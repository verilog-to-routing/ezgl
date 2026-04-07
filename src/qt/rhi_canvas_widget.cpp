#if defined(EZGL_QT) && defined(EZGL_RHI)

#include "ezgl/qt/rhi_canvas_widget.hpp"

#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QFile>
#include <QMutexLocker>
#include <chrono>
#include <limits>

#include "ezgl/qt/ezgl_qtcompat.hpp"

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
                      std::size_t                  needed_bytes,
                      std::size_t                  initial_bytes)
{
    if (buf && std::size_t(buf->size()) >= needed_bytes)
        return false;

    constexpr std::size_t kMaxQrhiBufferBytes =
        std::size_t(std::numeric_limits<int>::max());
    if (needed_bytes > kMaxQrhiBufferBytes) {
        qFatal("RhiCanvasWidget: requested GPU buffer size %zu exceeds QRhi int-sized limit %zu",
               needed_bytes,
               kMaxQrhiBufferBytes);
    }

    std::size_t new_size = buf ? std::size_t(buf->size()) : initial_bytes;
    while (new_size < needed_bytes) {
        if (new_size > kMaxQrhiBufferBytes / 2) {
            new_size = kMaxQrhiBufferBytes;
            break;
        }
        new_size *= 2;
    }
    if (new_size < needed_bytes) {
        qFatal("RhiCanvasWidget: failed to grow GPU buffer to %zu bytes without overflow",
               needed_bytes);
    }

    QRhiBuffer* old = buf.release();
    if (old)
        old->deleteLater(); // deferred: safe while a frame referencing it is in flight

    buf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, usage, int(new_size)));
    buf->create();
    return true;
}

struct ColorUniform {
    float rgba[4];
};
static_assert(sizeof(ColorUniform) == 16,
              "ColorUniform must match a std140 vec4");

struct PaletteUniformBlock {
    ColorUniform colors[ezgl::kMaxRhiStyleEntries];
};
static_assert(sizeof(PaletteUniformBlock) == ezgl::kMaxRhiStyleEntries * sizeof(ColorUniform),
              "PaletteUniformBlock must be tightly packed std140 vec4 entries");

ColorUniform makeColorUniform(std::uint32_t rgba)
{
    constexpr float kScale = 1.0f / 255.0f;
    return ColorUniform{{
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
    layout.setBindings({
        QRhiVertexInputBinding(sizeof(ezgl::PosVertex)),
        QRhiVertexInputBinding(sizeof(ezgl::StyleIndex))
    });
    layout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::PosVertex, x)),
        QRhiVertexInputAttribute(1, 1, QRhiVertexInputAttribute::UNormByte, 0)
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

void RhiCanvasWidget::set_frame_data(std::vector<PosVertex>     lines,
                                     std::vector<StyleIndex>    line_styles,
                                     std::vector<PosVertex>     fill_verts,
                                     std::vector<StyleIndex>    fill_styles,
                                     std::vector<PosVertex>     draw_verts,
                                     std::vector<StyleIndex>    draw_styles,
                                     std::vector<std::uint32_t> palette_rgba,
                                     const QMatrix4x4&          world_to_ndc,
                                     const QImage&              overlay,
                                     QColor                     bg_color)
{
    QMutexLocker lock(&m_frame_mutex);
    m_pending_lines        = std::move(lines);
    m_pending_line_styles  = std::move(line_styles);
    m_pending_fill         = std::move(fill_verts);
    m_pending_fill_styles  = std::move(fill_styles);
    m_pending_draw         = std::move(draw_verts);
    m_pending_draw_styles  = std::move(draw_styles);
    m_pending_palette_rgba = std::move(palette_rgba);
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

    // Uniform buffers: one MVP block plus a small shared palette.
    m_mvp_ubuf.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,
                                      QRhiBuffer::UniformBuffer,
                                      64));
    m_mvp_ubuf->create();
    m_palette_ubuf.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,
                                          QRhiBuffer::UniformBuffer,
                                          sizeof(PaletteUniformBlock)));
    m_palette_ubuf->create();

    // Vertex buffers: start at 1 MB for positions, style streams grow as needed.
    auto makeVBuf = [&]() {
        auto* b = rhi()->newBuffer(QRhiBuffer::Dynamic,
                                    QRhiBuffer::VertexBuffer,
                                    1 * 1024 * 1024);
        b->create();
        return b;
    };
    auto makeStyleBuf = [&]() {
        auto* b = rhi()->newBuffer(QRhiBuffer::Dynamic,
                                   QRhiBuffer::VertexBuffer,
                                   128 * 1024);
        b->create();
        return b;
    };
    m_line_vbuf.reset(makeVBuf());
    m_line_style_vbuf.reset(makeStyleBuf());
    m_fill_vbuf.reset(makeVBuf());
    m_fill_style_vbuf.reset(makeStyleBuf());
    m_draw_vbuf.reset(makeVBuf());
    m_draw_style_vbuf.reset(makeStyleBuf());

    // Shader resource bindings: MVP at binding 0, palette at binding 1.
    m_srb.reset(rhi()->newShaderResourceBindings());
    m_srb->setBindings({
        QRhiShaderResourceBinding::uniformBuffer(
            0,
            QRhiShaderResourceBinding::VertexStage,
            m_mvp_ubuf.get()),
        QRhiShaderResourceBinding::uniformBuffer(
            1,
            QRhiShaderResourceBinding::FragmentStage,
            m_palette_ubuf.get())
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

    const auto frame_start = std::chrono::steady_clock::now();

    // --- Snapshot pending frame under lock -----------------------------------
    std::vector<PosVertex>  lines, fill_verts, draw_verts;
    std::vector<StyleIndex> line_styles, fill_styles, draw_styles;
    std::vector<std::uint32_t> palette_rgba;
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
            line_styles  = std::move(m_pending_line_styles);
            fill_verts   = std::move(m_pending_fill);
            fill_styles  = std::move(m_pending_fill_styles);
            draw_verts   = std::move(m_pending_draw);
            draw_styles  = std::move(m_pending_draw_styles);
            palette_rgba = std::move(m_pending_palette_rgba);
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
        if (line_styles.size() != lines.size()
            || fill_styles.size() != fill_verts.size()
            || draw_styles.size() != draw_verts.size()) {
            qFatal("RhiCanvasWidget: style-stream size mismatch with vertex stream");
        }
        if (palette_rgba.size() > kMaxRhiStyleEntries) {
            qFatal("RhiCanvasWidget: palette size %zu exceeds limit %zu",
                   palette_rgba.size(), kMaxRhiStyleEntries);
        }

        // Geometry changed — upload positions, compact style indices, and palette.
        auto uploadVBuf = [&](std::unique_ptr<QRhiBuffer>& buf,
                              const auto&                    verts,
                              std::size_t                    initial_bytes) {
            if (verts.empty())
                return;

            const std::size_t bytes = verts.size() * sizeof(verts[0]);
            ensureDynamicBuf(rhi(), buf, QRhiBuffer::VertexBuffer, bytes, initial_bytes);
            u->updateDynamicBuffer(buf.get(), 0, int(bytes), verts.data());
        };
        uploadVBuf(m_line_vbuf, lines, 1 * 1024 * 1024);
        uploadVBuf(m_line_style_vbuf, line_styles, 128 * 1024);
        uploadVBuf(m_fill_vbuf, fill_verts, 1 * 1024 * 1024);
        uploadVBuf(m_fill_style_vbuf, fill_styles, 128 * 1024);
        uploadVBuf(m_draw_vbuf, draw_verts, 1 * 1024 * 1024);
        uploadVBuf(m_draw_style_vbuf, draw_styles, 128 * 1024);

        PaletteUniformBlock palette_data{};
        const std::size_t palette_count =
            std::min<std::size_t>(palette_rgba.size(), kMaxRhiStyleEntries);
        for (std::size_t i = 0; i < palette_count; ++i)
            palette_data.colors[i] = makeColorUniform(palette_rgba[i]);
        u->updateDynamicBuffer(m_palette_ubuf.get(),
                               0,
                               sizeof(PaletteUniformBlock),
                               &palette_data);

        m_line_count = quint32(lines.size());
        m_fill_count = quint32(fill_verts.size());
        m_draw_count = quint32(draw_verts.size());
    }
    // Camera-only frame: vertex/style buffers and palette are reused.

    // --- Record draw commands ------------------------------------------------
    cb->beginPass(renderTarget(), bg, { 1.0f, 0 }, u);
    cb->setViewport(QRhiViewport(0, 0, float(width()), float(height())));

    auto drawBatch = [&](std::unique_ptr<QRhiGraphicsPipeline>& pso,
                         std::unique_ptr<QRhiBuffer>&           pos_vbuf,
                         std::unique_ptr<QRhiBuffer>&           style_vbuf,
                         quint32                                count) {
        if (count == 0)
            return;

        cb->setGraphicsPipeline(pso.get());
        cb->setShaderResources(m_srb.get());
        const QRhiCommandBuffer::VertexInput inputs[] = {
            { pos_vbuf.get(), 0 },
            { style_vbuf.get(), 0 }
        };
        cb->setVertexInput(0, 2, inputs);
        cb->draw(count);
    };

    drawBatch(m_line_pso, m_line_vbuf, m_line_style_vbuf, m_line_count);
    drawBatch(m_fill_pso, m_fill_vbuf, m_fill_style_vbuf, m_fill_count);
    drawBatch(m_draw_pso, m_draw_vbuf, m_draw_style_vbuf, m_draw_count);

    cb->endPass();

    const auto frame_end = std::chrono::steady_clock::now();
    const double frame_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
    g_debug("RHI render() CPU time %.3f ms (geom_dirty=%d, line_verts=%u, fill_verts=%u, draw_verts=%u)",
            frame_ms,
            int(geom_dirty),
            m_line_count,
            m_fill_count,
            m_draw_count);
}

void RhiCanvasWidget::releaseResources()
{
    m_draw_pso.reset();
    m_fill_pso.reset();
    m_line_pso.reset();
    m_srb.reset();
    m_draw_style_vbuf.reset();
    m_draw_vbuf.reset();
    m_fill_style_vbuf.reset();
    m_fill_vbuf.reset();
    m_line_style_vbuf.reset();
    m_line_vbuf.reset();
    m_palette_ubuf.reset();
    m_mvp_ubuf.reset();
    m_line_count = 0;
    m_fill_count = 0;
    m_draw_count = 0;
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
