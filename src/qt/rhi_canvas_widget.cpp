#if defined(EZGL_QT) && defined(EZGL_RHI)

#include "ezgl/qt/rhi_canvas_widget.hpp"

#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QFile>
#include <QMutexLocker>
#include <cstring>

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
 * Ensure buf is a dynamic QRhi buffer of at least needed_bytes.
 * Grows by doubling; the old buffer is scheduled for deferred deletion.
 */
bool ensureDynamicBuf(QRhi*                        rhi,
                      std::unique_ptr<QRhiBuffer>& buf,
                      QRhiBuffer::UsageFlags       usage,
                      int                          needed_bytes,
                      int                          initial_bytes)
{
    if (buf && buf->size() >= needed_bytes)
        return false;

    int new_size = buf ? buf->size() * 2 : initial_bytes;
    while (new_size < needed_bytes)
        new_size *= 2;

    QRhiBuffer* old = buf.release();
    if (old)
        old->deleteLater(); // deferred: safe while a frame referencing it is in flight

    buf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, usage, new_size));
    buf->create();
    return true;
}

struct ColorUniformBlock {
    float rgba[4];
};
static_assert(sizeof(ColorUniformBlock) == 16,
              "ColorUniformBlock must match a std140 vec4");

ColorUniformBlock makeColorUniform(std::uint32_t rgba)
{
    constexpr float kScale = 1.0f / 255.0f;
    return ColorUniformBlock{{
        float((rgba >>  0) & 0xFF) * kScale,
        float((rgba >>  8) & 0xFF) * kScale,
        float((rgba >> 16) & 0xFF) * kScale,
        float((rgba >> 24) & 0xFF) * kScale
    }};
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
    layout.setBindings({ QRhiVertexInputBinding(sizeof(ezgl::PosVertex)) });
    layout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::PosVertex, x))
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

void RhiCanvasWidget::set_frame_data(std::vector<PosVertex>   lines,
                                     std::vector<ColorBatch>  line_batches,
                                     std::vector<PosVertex>   fill_verts,
                                     std::vector<ColorBatch>  fill_batches,
                                     std::vector<PosVertex>   draw_verts,
                                     std::vector<ColorBatch>  draw_batches,
                                     const QMatrix4x4&       world_to_ndc,
                                     const QImage&           overlay,
                                     QColor                  bg_color)
{
    QMutexLocker lock(&m_frame_mutex);
    m_pending_lines        = std::move(lines);
    m_pending_line_batches = std::move(line_batches);
    m_pending_fill         = std::move(fill_verts);
    m_pending_fill_batches = std::move(fill_batches);
    m_pending_draw         = std::move(draw_verts);
    m_pending_draw_batches = std::move(draw_batches);
    m_pending_mvp          = world_to_ndc;
    m_pending_overlay      = overlay;
    m_pending_bg           = bg_color;
    m_frame_dirty          = true;
    m_mvp_dirty            = false;  // superseded by full frame
}

void RhiCanvasWidget::set_mvp_only(const QMatrix4x4& world_to_ndc)
{
    QMutexLocker lock(&m_frame_mutex);
    m_pending_mvp = world_to_ndc;
    m_mvp_dirty   = true;
    // m_frame_dirty intentionally NOT set — vertex buffers are reused.
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

    // Uniform buffers: one MVP block plus dynamically-offset color blocks.
    m_mvp_ubuf.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,
                                      QRhiBuffer::UniformBuffer,
                                      64));
    m_mvp_ubuf->create();
    m_color_ubuf_stride = quint32(rhi()->ubufAligned(sizeof(ColorUniformBlock)));
    m_color_ubuf.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,
                                        QRhiBuffer::UniformBuffer,
                                        int(m_color_ubuf_stride * 256)));
    m_color_ubuf->create();

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

    // Shader resource bindings: MVP at binding 0, per-batch color at binding 1.
    m_srb.reset(rhi()->newShaderResourceBindings());
    m_srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0,
            QRhiShaderResourceBinding::VertexStage,
            m_mvp_ubuf.get()),
        QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
            1,
            QRhiShaderResourceBinding::FragmentStage,
            m_color_ubuf.get(),
            sizeof(ColorUniformBlock))
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
    std::vector<PosVertex>  lines, fill_verts, draw_verts;
    std::vector<ColorBatch> line_batches, fill_batches, draw_batches;
    QMatrix4x4 mvp;
    QColor     bg;
    bool       geom_dirty;

    {
        QMutexLocker lock(&m_frame_mutex);
        if (!m_frame_dirty && !m_mvp_dirty)
            return;

        geom_dirty = m_frame_dirty;
        mvp        = m_pending_mvp;
        bg         = m_pending_bg;

        if (geom_dirty) {
            lines        = std::move(m_pending_lines);
            line_batches = std::move(m_pending_line_batches);
            fill_verts   = std::move(m_pending_fill);
            fill_batches = std::move(m_pending_fill_batches);
            draw_verts   = std::move(m_pending_draw);
            draw_batches = std::move(m_pending_draw_batches);
        }
        m_frame_dirty = false;
        m_mvp_dirty   = false;
        // m_pending_overlay stays; paintEvent() reads it via the mutex.
    }

    // --- Upload to GPU -------------------------------------------------------
    QRhiResourceUpdateBatch* u = rhi()->nextResourceUpdateBatch();

    // Always update MVP (column-major, as OpenGL/SPIR-V expect).
    u->updateDynamicBuffer(m_mvp_ubuf.get(), 0, 64, mvp.constData());

    if (geom_dirty) {
        // Geometry changed — upload position buffers and cache batches.
        auto uploadVBuf = [&](std::unique_ptr<QRhiBuffer>& buf,
                              const std::vector<PosVertex>& verts) {
            if (verts.empty())
                return;

            const int bytes = int(verts.size() * sizeof(PosVertex));
            ensureDynamicBuf(rhi(), buf, QRhiBuffer::VertexBuffer, bytes, 1 * 1024 * 1024);
            u->updateDynamicBuffer(buf.get(), 0, bytes, verts.data());
        };
        uploadVBuf(m_line_vbuf, lines);
        uploadVBuf(m_fill_vbuf, fill_verts);
        uploadVBuf(m_draw_vbuf, draw_verts);

        const std::size_t total_batches =
            line_batches.size() + fill_batches.size() + draw_batches.size();
        const int needed_color_bytes = int(std::max<std::size_t>(1, total_batches) * m_color_ubuf_stride);
        const bool color_resized = ensureDynamicBuf(
            rhi(), m_color_ubuf, QRhiBuffer::UniformBuffer, needed_color_bytes, needed_color_bytes);

        if (color_resized) {
            m_srb->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0,
                    QRhiShaderResourceBinding::VertexStage,
                    m_mvp_ubuf.get()),
                QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                    1,
                    QRhiShaderResourceBinding::FragmentStage,
                    m_color_ubuf.get(),
                    sizeof(ColorUniformBlock))
            });
            m_srb->create();
        }

        if (total_batches > 0) {
            std::vector<char> color_data(total_batches * m_color_ubuf_stride, 0);
            std::size_t batch_index = 0;
            auto appendColorData = [&](const std::vector<ColorBatch>& batches) {
                for (const ColorBatch& batch : batches) {
                    const ColorUniformBlock uniform = makeColorUniform(batch.color_rgba);
                    std::memcpy(color_data.data() + batch_index * m_color_ubuf_stride,
                                &uniform,
                                sizeof(uniform));
                    ++batch_index;
                }
            };
            appendColorData(line_batches);
            appendColorData(fill_batches);
            appendColorData(draw_batches);
            u->updateDynamicBuffer(m_color_ubuf.get(),
                                   0,
                                   int(color_data.size()),
                                   color_data.data());
        }

        m_line_batches = std::move(line_batches);
        m_fill_batches = std::move(fill_batches);
        m_draw_batches = std::move(draw_batches);
    }
    // Camera-only frame: vertex buffers and batch metadata are reused.

    // --- Record draw commands ------------------------------------------------
    cb->beginPass(renderTarget(), bg, { 1.0f, 0 }, u);
    cb->setViewport(QRhiViewport(0, 0, float(width()), float(height())));

    auto drawBatches = [&](std::unique_ptr<QRhiGraphicsPipeline>& pso,
                           std::unique_ptr<QRhiBuffer>&           vbuf,
                           const std::vector<ColorBatch>&         batches,
                           quint32                                batch_base) {
        if (batches.empty())
            return;

        cb->setGraphicsPipeline(pso.get());
        const QRhiCommandBuffer::VertexInput vi(vbuf.get(), 0);
        cb->setVertexInput(0, 1, &vi);

        for (std::size_t i = 0; i < batches.size(); ++i) {
            const ColorBatch& batch = batches[i];
            const QRhiCommandBuffer::DynamicOffset offset(
                1, (batch_base + quint32(i)) * m_color_ubuf_stride);
            cb->setShaderResources(m_srb.get(), 1, &offset);
            cb->draw(batch.count, 1, batch.first);
        }
    };

    const quint32 line_batch_base = 0;
    const quint32 fill_batch_base = quint32(m_line_batches.size());
    const quint32 draw_batch_base = fill_batch_base + quint32(m_fill_batches.size());

    drawBatches(m_line_pso, m_line_vbuf, m_line_batches, line_batch_base);
    drawBatches(m_fill_pso, m_fill_vbuf, m_fill_batches, fill_batch_base);
    drawBatches(m_draw_pso, m_draw_vbuf, m_draw_batches, draw_batch_base);

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
    m_color_ubuf.reset();
    m_mvp_ubuf.reset();
    m_line_batches.clear();
    m_fill_batches.clear();
    m_draw_batches.clear();
    m_color_ubuf_stride = 0;
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
