#if defined(EZGL_QT) && defined(EZGL_RHI)

#include "ezgl/qt/rhi_canvas_widget.hpp"

#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QFile>
#include <QMutexLocker>

// Q_INIT_RESOURCE must be called at global scope (not inside a namespace).
// For static libraries, Qt resources are not automatically registered, so
// we force initialization once via a file-scope static.
static const int s_rhi_resources_init = []() {
    Q_INIT_RESOURCE(shaders);
    return 0;
}();

namespace {

// ---- file-scope helpers (not exposed in header) ----------------------------

QShader loadShader(const char* resource_path)
{
    QFile f(resource_path);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("RhiCanvasWidget: cannot open shader resource %s", resource_path);
    return QShader::fromSerialized(f.readAll());
}

/**
 * Ensure buf is a Dynamic VertexBuffer of at least needed_bytes.
 * Grows by doubling; the old buffer is scheduled for deferred deletion.
 */
void ensureVBuf(QRhi* rhi, std::unique_ptr<QRhiBuffer>& buf, int needed_bytes)
{
    if (buf && buf->size() >= needed_bytes)
        return;

    int new_size = buf ? buf->size() * 2 : 1 * 1024 * 1024; // 1 MB initial
    while (new_size < needed_bytes)
        new_size *= 2;

    QRhiBuffer* old = buf.release();
    if (old)
        old->deleteLater(); // deferred: safe while a frame referencing it is in flight

    buf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer, new_size));
    buf->create();
}

void buildPipeline(QRhi*                                    rhi,
                   std::unique_ptr<QRhiGraphicsPipeline>&   pso,
                   QRhiGraphicsPipeline::Topology            topology,
                   const QShader&                            vs,
                   const QShader&                            fs,
                   QRhiShaderResourceBindings*               srb,
                   QRhiRenderPassDescriptor*                  rpDesc)
{
    QRhiVertexInputLayout layout;
    layout.setBindings({ QRhiVertexInputBinding(sizeof(ezgl::LineVertex)) });
    layout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::LineVertex, x)),
        QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::UNormByte4,
                                 offsetof(ezgl::LineVertex, r))
    });

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable   = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    pso.reset(rhi->newGraphicsPipeline());
    pso->setTopology(topology);
    pso->setVertexInputLayout(layout);
    pso->setShaderStages({
        { QRhiShaderStage::Vertex,   vs },
        { QRhiShaderStage::Fragment, fs }
    });
    pso->setShaderResourceBindings(srb);
    pso->setRenderPassDescriptor(rpDesc);
    pso->setTargetBlends({ blend });
    pso->setDepthTest(false);
    pso->setDepthWrite(false);
    pso->create();
}

} // anonymous namespace

// ---- RhiCanvasWidget -------------------------------------------------------

namespace ezgl {

RhiCanvasWidget::RhiCanvasWidget(QWidget* parent)
    : QRhiWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    // mirrorVertically=false: our MVP (ortho with y-down origin) already
    // produces correct screen orientation without an extra vertical flip.
    setMirrorVertically(false);
}

// Destructor defined here so QRhiBuffer / QRhiGraphicsPipeline are complete.
RhiCanvasWidget::~RhiCanvasWidget() = default;

// ---- public API ------------------------------------------------------------

void RhiCanvasWidget::set_frame_data(std::vector<LineVertex> lines,
                                      std::vector<LineVertex> fill_verts,
                                      std::vector<LineVertex> draw_verts,
                                      const QMatrix4x4&       screen_to_ndc,
                                      const QImage&           overlay,
                                      QColor                  bg_color)
{
    QMutexLocker lock(&m_frame_mutex);
    m_pending_lines   = std::move(lines);
    m_pending_fill    = std::move(fill_verts);
    m_pending_draw    = std::move(draw_verts);
    m_pending_mvp     = screen_to_ndc;
    m_pending_overlay = overlay;
    m_pending_bg      = bg_color;
    m_frame_dirty     = true;
}

void RhiCanvasWidget::setResizeCallback(std::function<void(int,int)> cb)
{
    m_resize_cb = std::move(cb);
}

void RhiCanvasWidget::setPreResizeCallback(std::function<void()> cb)
{
    m_pre_resize_cb = std::move(cb);
}

// ---- QRhiWidget overrides --------------------------------------------------

void RhiCanvasWidget::initialize(QRhiCommandBuffer* /*cb*/)
{
    QShader vs = loadShader(":/ezgl/line.vert.qsb");
    QShader fs = loadShader(":/ezgl/line.frag.qsb");

    // Uniform buffer: one mat4 (64 bytes).
    m_ubuf.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,
                                   QRhiBuffer::UniformBuffer, 64));
    m_ubuf->create();

    // Vertex buffers: start at 1 MB, grow on demand in render().
    auto makeVBuf = [&]() {
        auto* b = rhi()->newBuffer(QRhiBuffer::Dynamic,
                                    QRhiBuffer::VertexBuffer,
                                    1 * 1024 * 1024);
        b->create();
        return b;
    };
    m_line_vbuf.reset(makeVBuf());
    m_fill_vbuf.reset(makeVBuf());
    m_draw_vbuf.reset(makeVBuf());

    // Shader resource bindings: UBO at binding 0, vertex stage only.
    m_srb.reset(rhi()->newShaderResourceBindings());
    m_srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0,
            QRhiShaderResourceBinding::VertexStage,
            m_ubuf.get())
    });
    m_srb->create();

    auto* rpDesc = renderTarget()->renderPassDescriptor();
    buildPipeline(rhi(), m_line_pso, QRhiGraphicsPipeline::Lines,     vs, fs, m_srb.get(), rpDesc);
    buildPipeline(rhi(), m_fill_pso, QRhiGraphicsPipeline::Triangles, vs, fs, m_srb.get(), rpDesc);
    buildPipeline(rhi(), m_draw_pso, QRhiGraphicsPipeline::Lines,     vs, fs, m_srb.get(), rpDesc);

    m_initialized = true;
}

void RhiCanvasWidget::render(QRhiCommandBuffer* cb)
{
    if (!m_initialized)
        return;

    // --- Snapshot pending frame under lock -----------------------------------
    std::vector<LineVertex> lines, fill_verts, draw_verts;
    QMatrix4x4 mvp;
    QColor     bg;

    {
        QMutexLocker lock(&m_frame_mutex);
        if (!m_frame_dirty)
            return;
        lines      = std::move(m_pending_lines);
        fill_verts = std::move(m_pending_fill);
        draw_verts = std::move(m_pending_draw);
        mvp        = m_pending_mvp;
        bg         = m_pending_bg;
        m_frame_dirty = false;
        // m_pending_overlay stays; paintEvent() reads it via the mutex.
    }

    // --- Upload to GPU -------------------------------------------------------
    QRhiResourceUpdateBatch* u = rhi()->nextResourceUpdateBatch();

    // MVP (column-major, as OpenGL/SPIR-V expect).
    u->updateDynamicBuffer(m_ubuf.get(), 0, 64, mvp.constData());

    auto uploadVBuf = [&](std::unique_ptr<QRhiBuffer>& buf,
                          const std::vector<LineVertex>& verts) {
        if (verts.empty()) return;
        const int bytes = int(verts.size() * sizeof(LineVertex));
        ensureVBuf(rhi(), buf, bytes);
        u->updateDynamicBuffer(buf.get(), 0, bytes, verts.data());
    };
    uploadVBuf(m_line_vbuf, lines);
    uploadVBuf(m_fill_vbuf, fill_verts);
    uploadVBuf(m_draw_vbuf, draw_verts);

    // --- Record draw commands ------------------------------------------------
    cb->beginPass(renderTarget(), bg, { 1.0f, 0 }, u);
    cb->setViewport(QRhiViewport(0, 0, float(width()), float(height())));

    auto drawBatch = [&](std::unique_ptr<QRhiGraphicsPipeline>& pso,
                          std::unique_ptr<QRhiBuffer>&           vbuf,
                          const std::vector<LineVertex>&         verts) {
        if (verts.empty()) return;
        cb->setGraphicsPipeline(pso.get());
        cb->setShaderResources();
        const QRhiCommandBuffer::VertexInput vi(vbuf.get(), 0);
        cb->setVertexInput(0, 1, &vi);
        cb->draw(quint32(verts.size()));
    };

    drawBatch(m_line_pso, m_line_vbuf, lines);
    drawBatch(m_fill_pso, m_fill_vbuf, fill_verts);
    drawBatch(m_draw_pso, m_draw_vbuf, draw_verts);

    cb->endPass();
}

void RhiCanvasWidget::releaseResources()
{
    m_draw_pso.reset();
    m_fill_pso.reset();
    m_line_pso.reset();
    m_srb.reset();
    m_draw_vbuf.reset();
    m_fill_vbuf.reset();
    m_line_vbuf.reset();
    m_ubuf.reset();
    m_initialized = false;
}

void RhiCanvasWidget::paintEvent(QPaintEvent* e)
{
    // QRhiWidget::paintEvent renders the GPU texture to the widget surface.
    QRhiWidget::paintEvent(e);

    // Composite QPainter overlay (text, arcs, polygons) on top.
    QImage overlay;
    {
        QMutexLocker lock(&m_frame_mutex);
        overlay = m_pending_overlay; // shallow COW copy
    }
    if (!overlay.isNull()) {
        QPainter p(this);
        p.drawImage(rect(), overlay, overlay.rect());
    }
}

void RhiCanvasWidget::resizeEvent(QResizeEvent* e)
{
    if (m_pre_resize_cb)
        m_pre_resize_cb();
    QRhiWidget::resizeEvent(e);
    if (width() > 0 && height() > 0 && m_resize_cb)
        m_resize_cb(width(), height());
}

} // namespace ezgl

#endif // EZGL_QT && EZGL_RHI
