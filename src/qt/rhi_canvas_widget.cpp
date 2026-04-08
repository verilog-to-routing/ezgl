#if defined(EZGL_QT) && defined(EZGL_RHI)

#include "ezgl/qt/rhi_canvas_widget.hpp"

#include <algorithm>
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
#if defined(EZGL_RHI_GENERATED_SHADERS)
    Q_INIT_RESOURCE(ezgl_rhi_shaders);
#else
    Q_INIT_RESOURCE(shaders);
#endif
    return 0;
}();

namespace {

// ---- file-scope helpers (not exposed in header) ----------------------------

constexpr std::size_t kMaxQrhiBufferBytes =
    std::size_t(std::numeric_limits<int>::max());
constexpr std::size_t kInitialStreamVertexBufferBytes      = 1 * 1024 * 1024;
constexpr std::size_t kInitialStreamStyleBufferBytes       = 128 * 1024;
constexpr std::size_t kInitialThickInstanceBufferBytes  = 512 * 1024;
constexpr std::size_t kInitialDashedInstanceBufferBytes = 512 * 1024;
constexpr std::size_t kMaxVerticesPerChunk =
    std::min(kMaxQrhiBufferBytes / sizeof(ezgl::PosVertex),
             kMaxQrhiBufferBytes / sizeof(ezgl::StyleIndex));
constexpr std::size_t kMaxThickInstancesPerChunk =
    std::min(kMaxQrhiBufferBytes / sizeof(ezgl::ThickLineInstance),
             kMaxQrhiBufferBytes / sizeof(ezgl::StyleIndex));
constexpr std::size_t kMaxDashedInstancesPerChunk =
    std::min(kMaxQrhiBufferBytes / sizeof(ezgl::DashedLineInstance),
             kMaxQrhiBufferBytes / sizeof(ezgl::StyleIndex));

// MVP UBO layout (std140, binding 0):
//   offset  0 : mat4  mvp      (64 bytes)
//   offset 64 : vec2  viewport (8 bytes — widget size in pixels)
//   padding   : 8 bytes  →  total 80 bytes (16-byte aligned)
static constexpr int kMvpUboSize = 80;

int currentFrameResourceIndex(QRhi* rhi, std::size_t frame_count)
{
    if (frame_count == 0)
        return 0;

    const int max_index = int(frame_count - 1);
    return std::clamp(rhi->currentFrameSlot(), 0, max_index);
}

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

void releaseDynamicBuf(std::unique_ptr<QRhiBuffer>& buf)
{
    QRhiBuffer* old = buf.release();
    if (old)
        old->deleteLater();
}

bool rectanglesIntersect(const ezgl::rectangle& a, const ezgl::rectangle& b)
{
    return !(a.right() < b.left()
          || a.left() > b.right()
          || a.top() < b.bottom()
          || a.bottom() > b.top());
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

/**
 * Build the thick-line pipeline.
 *
 * Vertex format uses ThickLineVertex (24 bytes) in binding 0 and StyleIndex
 * (1 byte) in binding 1.  The vertex shader (thick_line.vert) expands each
 * vertex perpendicularly using the MVP + viewport uniforms.
 * The fragment shader is the same line.frag palette-lookup shader.
 */
void buildThickLinePipeline(QRhi*                                   rhi,
                            std::unique_ptr<QRhiGraphicsPipeline>&  pso,
                            const QShader&                           thick_vs,
                            const QShader&                           fs,
                            QRhiShaderResourceBindings*              srb,
                            QRhiRenderPassDescriptor*                rpDesc)
{
    // Instanced rendering:
    //   Binding 0 (PerVertex)   — QuadCorner   (t, side)        — 4 corners, constant
    //   Binding 1 (PerInstance) — ThickLineInstance (x0,y0,x1,y1,width_px)
    //   Binding 2 (PerInstance) — StyleIndex   (normalised byte) — 1 per line
    QRhiVertexInputLayout layout;
    layout.setBindings({
        QRhiVertexInputBinding(sizeof(ezgl::QuadCorner)),
        QRhiVertexInputBinding(sizeof(ezgl::ThickLineInstance),
                               QRhiVertexInputBinding::PerInstance),
        QRhiVertexInputBinding(sizeof(ezgl::StyleIndex),
                               QRhiVertexInputBinding::PerInstance)
    });
    layout.setAttributes({
        // location 0: inCorner (t, side) — from binding 0, per vertex
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::QuadCorner, t)),
        // location 1: inStart (x0, y0) — from binding 1, per instance
        QRhiVertexInputAttribute(1, 1, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::ThickLineInstance, x0)),
        // location 2: inEnd (x1, y1) — from binding 1, per instance
        QRhiVertexInputAttribute(1, 2, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::ThickLineInstance, x1)),
        // location 3: inWidthPx — from binding 1, per instance
        QRhiVertexInputAttribute(1, 3, QRhiVertexInputAttribute::Float,
                                 offsetof(ezgl::ThickLineInstance, width_px)),
        // location 4: inStyleNorm — from binding 2, per instance
        QRhiVertexInputAttribute(2, 4, QRhiVertexInputAttribute::UNormByte, 0)
    });

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable   = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    pso.reset(rhi->newGraphicsPipeline());
    pso->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    pso->setVertexInputLayout(layout);
    pso->setShaderStages({
        { QRhiShaderStage::Vertex,   thick_vs },
        { QRhiShaderStage::Fragment, fs }
    });
    pso->setShaderResourceBindings(srb);
    pso->setRenderPassDescriptor(rpDesc);
    pso->setTargetBlends({ blend });
    pso->setDepthTest(false);
    pso->setDepthWrite(false);
    pso->create();
}

void buildDashedLinePipeline(QRhi*                                   rhi,
                             std::unique_ptr<QRhiGraphicsPipeline>&  pso,
                             const QShader&                           dashed_vs,
                             const QShader&                           dashed_fs,
                             QRhiShaderResourceBindings*              srb,
                             QRhiRenderPassDescriptor*                rpDesc)
{
    // Binding 0 (PerVertex)   — QuadCorner (t, side)
    // Binding 1 (PerInstance) — DashedLineInstance
    // Binding 2 (PerInstance) — StyleIndex
    QRhiVertexInputLayout layout;
    layout.setBindings({
        QRhiVertexInputBinding(sizeof(ezgl::QuadCorner)),
        QRhiVertexInputBinding(sizeof(ezgl::DashedLineInstance),
                               QRhiVertexInputBinding::PerInstance),
        QRhiVertexInputBinding(sizeof(ezgl::StyleIndex),
                               QRhiVertexInputBinding::PerInstance)
    });
    layout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::QuadCorner, t)),
        QRhiVertexInputAttribute(1, 1, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::DashedLineInstance, x0)),
        QRhiVertexInputAttribute(1, 2, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::DashedLineInstance, x1)),
        QRhiVertexInputAttribute(1, 3, QRhiVertexInputAttribute::Float,
                                 offsetof(ezgl::DashedLineInstance, width_px)),
        QRhiVertexInputAttribute(1, 4, QRhiVertexInputAttribute::Float,
                                 offsetof(ezgl::DashedLineInstance, dash_px)),
        QRhiVertexInputAttribute(1, 5, QRhiVertexInputAttribute::Float,
                                 offsetof(ezgl::DashedLineInstance, gap_px)),
        QRhiVertexInputAttribute(1, 6, QRhiVertexInputAttribute::Float,
                                 offsetof(ezgl::DashedLineInstance, phase_world)),
        QRhiVertexInputAttribute(2, 7, QRhiVertexInputAttribute::UNormByte, 0)
    });

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable   = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    pso.reset(rhi->newGraphicsPipeline());
    pso->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    pso->setVertexInputLayout(layout);
    pso->setShaderStages({
        { QRhiShaderStage::Vertex,   dashed_vs },
        { QRhiShaderStage::Fragment, dashed_fs }
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

void RhiCanvasWidget::set_frame_data(std::vector<RhiTileBatch>  tiles,
                                     std::vector<std::uint32_t> palette_rgba,
                                     const QMatrix4x4&          world_to_ndc,
                                     const rectangle&           visible_world,
                                     const QImage&              overlay,
                                     QColor                     bg_color)
{
    QMutexLocker lock(&m_frame_mutex);
    m_pending_tiles        = std::move(tiles);
    m_pending_palette_rgba = std::move(palette_rgba);
    m_pending_mvp          = world_to_ndc;
    m_pending_visible_world = visible_world;
    m_pending_overlay      = overlay;
    m_pending_bg           = bg_color;
    m_frame_dirty          = true;
    m_mvp_dirty            = false;  // superseded by full frame
}

void RhiCanvasWidget::set_mvp_only(const QMatrix4x4& world_to_ndc,
                                   const rectangle&  visible_world)
{
    QMutexLocker lock(&m_frame_mutex);
    m_pending_mvp = world_to_ndc;
    m_pending_visible_world = visible_world;
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
    QShader vs          = loadShader(":/ezgl/line.vert.qsb");
    QShader fs          = loadShader(":/ezgl/line.frag.qsb");
    QShader thick_vs    = loadShader(":/ezgl/thick_line.vert.qsb");
    QShader dashed_vs   = loadShader(":/ezgl/dashed_line.vert.qsb");
    QShader dashed_fs   = loadShader(":/ezgl/dashed_line.frag.qsb");

    m_frame_resources.clear();
    m_frame_resources.resize(std::max(1, rhi()->resourceLimit(QRhi::FramesInFlight)));

    for (FrameResources& frame : m_frame_resources) {
        // MVP UBO: 80 bytes — mat4 mvp (64 B) + vec2 viewport (8 B) + 8 B padding.
        // All vertex shaders share the same std140 block layout:
        //   mat4 mvp + vec2 viewport.
        frame.mvp_ubuf.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,
                                              QRhiBuffer::UniformBuffer,
                                              kMvpUboSize));
        frame.mvp_ubuf->create();
        frame.palette_ubuf.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,
                                                  QRhiBuffer::UniformBuffer,
                                                  sizeof(PaletteUniformBlock)));
        frame.palette_ubuf->create();
        frame.line_vbufs.clear();
        frame.line_style_vbufs.clear();
        frame.fill_vbufs.clear();
        frame.fill_style_vbufs.clear();
        frame.draw_vbufs.clear();
        frame.draw_style_vbufs.clear();
        frame.thick_line_instance_vbufs.clear();
        frame.thick_line_style_vbufs.clear();
        frame.dashed_line_instance_vbufs.clear();
        frame.dashed_line_style_vbufs.clear();
        frame.gpu_tiles.clear();
    }

    // Constant quad-corner buffer (32 bytes, Dynamic so we can upload via
    // the normal updateDynamicBuffer path in render()).
    // 4 corners for TriangleStrip: {t=0,side=-1},{t=0,+1},{t=1,-1},{t=1,+1}
    m_thick_line_corner_vbuf.reset(
        rhi()->newBuffer(QRhiBuffer::Dynamic,
                         QRhiBuffer::VertexBuffer,
                         int(4 * sizeof(ezgl::QuadCorner))));
    m_thick_line_corner_vbuf->create();

    // Shader resource bindings: MVP at binding 0, palette at binding 1.
    // All pipelines share the same binding layout, but each in-flight frame
    // gets its own concrete buffers to avoid cross-frame aliasing.
    for (FrameResources& frame : m_frame_resources) {
        frame.srb.reset(rhi()->newShaderResourceBindings());
        frame.srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::VertexStage,
                frame.mvp_ubuf.get()),
            QRhiShaderResourceBinding::uniformBuffer(
                1,
                QRhiShaderResourceBinding::FragmentStage,
                frame.palette_ubuf.get())
        });
        frame.srb->create();
    }

    auto* rpDesc = renderTarget()->renderPassDescriptor();
    QRhiShaderResourceBindings* const pipeline_srb = m_frame_resources.front().srb.get();
    buildPipeline(rhi(), m_line_pso,  QRhiGraphicsPipeline::Lines,     vs, fs, pipeline_srb, rpDesc);
    buildPipeline(rhi(), m_fill_pso,  QRhiGraphicsPipeline::Triangles, vs, fs, pipeline_srb, rpDesc);
    buildPipeline(rhi(), m_draw_pso,  QRhiGraphicsPipeline::Lines,     vs, fs, pipeline_srb, rpDesc);
    buildThickLinePipeline(rhi(), m_thick_line_pso, thick_vs, fs, pipeline_srb, rpDesc);
    buildDashedLinePipeline(rhi(), m_dashed_line_pso, dashed_vs, dashed_fs, pipeline_srb, rpDesc);

    m_initialized = true;
}

void RhiCanvasWidget::render(QRhiCommandBuffer* cb)
{
    if (!m_initialized || m_frame_resources.empty())
        return;

    const auto frame_start = std::chrono::steady_clock::now();

    // --- Snapshot pending frame under lock -----------------------------------
    std::vector<RhiTileBatch> tiles;
    std::vector<std::uint32_t> palette_rgba;
    QMatrix4x4 mvp;
    rectangle  visible_world;
    QColor     bg;
    bool       geom_dirty;

    {
        QMutexLocker lock(&m_frame_mutex);
        if (!m_frame_dirty && !m_mvp_dirty)
            return;

        geom_dirty = m_frame_dirty;
        mvp        = m_pending_mvp;
        visible_world = m_pending_visible_world;
        bg         = m_pending_bg;

        if (geom_dirty) {
            tiles        = std::move(m_pending_tiles);
            palette_rgba = std::move(m_pending_palette_rgba);
        }
        m_frame_dirty = false;
        m_mvp_dirty   = false;
        // m_pending_overlay stays; paintEvent() reads it via the mutex.
    }

    const int frame_slot = currentFrameResourceIndex(rhi(), m_frame_resources.size());
    FrameResources& frame = m_frame_resources[std::size_t(frame_slot)];

    // --- Upload to GPU -------------------------------------------------------
    QRhiResourceUpdateBatch* u = rhi()->nextResourceUpdateBatch();

    // Update MVP (column-major, as OpenGL/SPIR-V expect) + viewport.
    // thick_line.vert reads the viewport at offset 64 in the same UBO.
    u->updateDynamicBuffer(frame.mvp_ubuf.get(), 0, 64, mvp.constData());
    {
        const float vp[2] = { float(width()), float(height()) };
        u->updateDynamicBuffer(frame.mvp_ubuf.get(), 64, int(sizeof(vp)), vp);
    }

    // Upload the constant quad-corner buffer (32 bytes, same every frame).
    // Done unconditionally so it is valid even after releaseResources/re-init.
    {
        static const ezgl::QuadCorner kCorners[4] = {
            { 0.0f, -1.0f }, // t=start, left edge
            { 0.0f, +1.0f }, // t=start, right edge
            { 1.0f, -1.0f }, // t=end,   left edge
            { 1.0f, +1.0f }  // t=end,   right edge
        };
        u->updateDynamicBuffer(m_thick_line_corner_vbuf.get(), 0,
                               int(sizeof(kCorners)), kCorners);
    }

    if (geom_dirty) {
        if (palette_rgba.size() > kMaxRhiStyleEntries) {
            qFatal("RhiCanvasWidget: palette size %zu exceeds limit %zu",
                   palette_rgba.size(), kMaxRhiStyleEntries);
        }

        struct PendingUpload {
            quint32             buffer_index   = 0;
            quint32             pos_offset     = 0;  // byte offset in position buffer
            quint32             style_offset   = 0;
            quint32             count          = 0;
            const void*         pos_data       = nullptr;
            const StyleIndex*   style_data     = nullptr;
            std::size_t         pos_vertex_size = 0; // sizeof(PosVertex) or sizeof(ThickLineVertex)
        };

        std::size_t total_line_vertices       = 0;
        std::size_t total_fill_vertices       = 0;
        std::size_t total_draw_vertices       = 0;
        std::size_t total_thick_line_vertices  = 0;
        std::size_t total_dashed_line_vertices = 0;
        for (const RhiTileBatch& tile : tiles) {
            if (tile.line_styles.size()         != tile.line_verts.size()
                || tile.fill_styles.size()      != tile.fill_verts.size()
                || tile.draw_styles.size()      != tile.draw_verts.size()
                || tile.thick_line_styles.size()  != tile.thick_line_instances.size()
                || tile.dashed_line_styles.size() != tile.dashed_line_instances.size()) {
                qFatal("RhiCanvasWidget: style-stream size mismatch with vertex stream");
            }

            total_line_vertices        += tile.line_verts.size();
            total_fill_vertices        += tile.fill_verts.size();
            total_draw_vertices        += tile.draw_verts.size();
            total_thick_line_vertices  += tile.thick_line_instances.size();
            total_dashed_line_vertices += tile.dashed_line_instances.size();
        }

        std::vector<std::size_t> line_buffer_vertices;
        std::vector<std::size_t> fill_buffer_vertices;
        std::vector<std::size_t> draw_buffer_vertices;
        std::vector<std::size_t> thick_line_buffer_vertices;
        std::vector<std::size_t> dashed_line_buffer_vertices;
        std::vector<PendingUpload> line_uploads;
        std::vector<PendingUpload> fill_uploads;
        std::vector<PendingUpload> draw_uploads;
        std::vector<PendingUpload> thick_line_uploads;
        std::vector<PendingUpload> dashed_line_uploads;
        line_uploads.reserve(
            (total_line_vertices + kMaxVerticesPerChunk - 1) / kMaxVerticesPerChunk);
        fill_uploads.reserve(
            (total_fill_vertices + kMaxVerticesPerChunk - 1) / kMaxVerticesPerChunk);
        draw_uploads.reserve(
            (total_draw_vertices + kMaxVerticesPerChunk - 1) / kMaxVerticesPerChunk);
        thick_line_uploads.reserve(
            (total_thick_line_vertices + kMaxThickInstancesPerChunk - 1)
            / kMaxThickInstancesPerChunk);
        dashed_line_uploads.reserve(
            (total_dashed_line_vertices + kMaxDashedInstancesPerChunk - 1)
            / kMaxDashedInstancesPerChunk);

        // planStream: distribute a tile's vertex+style arrays into chunked upload
        // records.  max_per_chunk and vertex_size are per-stream (PosVertex vs
        // ThickLineVertex differ in size and allowed chunk limit).
        auto planStream = [](std::vector<StreamChunk>&   chunks,
                             std::vector<PendingUpload>& uploads,
                             std::vector<std::size_t>&   buffer_vertex_counts,
                             const auto&                 verts,
                             const auto&                 styles,
                             std::size_t                 max_per_chunk,
                             std::size_t                 vertex_size) {
            if (styles.size() != verts.size()) {
                qFatal("RhiCanvasWidget: style-stream size mismatch with vertex stream");
            }

            chunks.clear();
            for (std::size_t begin = 0; begin < verts.size(); ) {
                if (buffer_vertex_counts.empty()
                        || buffer_vertex_counts.back() == max_per_chunk)
                    buffer_vertex_counts.push_back(0);

                const std::size_t buffer_index  = buffer_vertex_counts.size() - 1;
                const std::size_t vertex_offset = buffer_vertex_counts.back();
                const std::size_t count =
                    std::min(verts.size() - begin, max_per_chunk - vertex_offset);

                const std::size_t pos_offset   = vertex_offset * vertex_size;
                const std::size_t style_offset = vertex_offset * sizeof(StyleIndex);
                if (pos_offset > kMaxQrhiBufferBytes || style_offset > kMaxQrhiBufferBytes) {
                    qFatal("RhiCanvasWidget: planned stream offset exceeds QRhi int-sized limit");
                }

                chunks.push_back(StreamChunk{
                    quint32(buffer_index),
                    quint32(pos_offset),
                    quint32(style_offset),
                    quint32(count)
                });
                uploads.push_back(PendingUpload{
                    quint32(buffer_index),
                    quint32(pos_offset),
                    quint32(style_offset),
                    quint32(count),
                    static_cast<const void*>(verts.data() + begin),
                    styles.data() + begin,
                    vertex_size
                });
                buffer_vertex_counts.back() += count;
                begin += count;
            }
        };

        frame.gpu_tiles.resize(tiles.size());

        for (std::size_t i = 0; i < tiles.size(); ++i) {
            const RhiTileBatch& tile     = tiles[i];
            GpuTileBatch&       gpu_tile = frame.gpu_tiles[i];
            gpu_tile.world_bounds = tile.world_bounds;
            planStream(gpu_tile.line_chunks, line_uploads, line_buffer_vertices,
                       tile.line_verts, tile.line_styles,
                       kMaxVerticesPerChunk, sizeof(PosVertex));
            planStream(gpu_tile.fill_chunks, fill_uploads, fill_buffer_vertices,
                       tile.fill_verts, tile.fill_styles,
                       kMaxVerticesPerChunk, sizeof(PosVertex));
            planStream(gpu_tile.draw_chunks, draw_uploads, draw_buffer_vertices,
                       tile.draw_verts, tile.draw_styles,
                       kMaxVerticesPerChunk, sizeof(PosVertex));
            planStream(gpu_tile.thick_line_chunks, thick_line_uploads,
                       thick_line_buffer_vertices,
                       tile.thick_line_instances, tile.thick_line_styles,
                       kMaxThickInstancesPerChunk, sizeof(ThickLineInstance));
            planStream(gpu_tile.dashed_line_chunks, dashed_line_uploads,
                       dashed_line_buffer_vertices,
                       tile.dashed_line_instances, tile.dashed_line_styles,
                       kMaxDashedInstancesPerChunk, sizeof(DashedLineInstance));
        }

        auto trimBufferVector = [](std::vector<std::unique_ptr<QRhiBuffer>>& buffers,
                                   std::size_t                               keep_count) {
            for (std::size_t i = keep_count; i < buffers.size(); ++i)
                releaseDynamicBuf(buffers[i]);
            buffers.resize(keep_count);
        };
        auto ensureBufferSet = [&](std::vector<std::unique_ptr<QRhiBuffer>>& pos_vbufs,
                                   std::vector<std::unique_ptr<QRhiBuffer>>& style_vbufs,
                                   const std::vector<std::size_t>&           vertex_counts,
                                   std::size_t                               vertex_size,
                                   std::size_t                               initial_pos_bytes) {
            if (pos_vbufs.size() < vertex_counts.size())
                pos_vbufs.resize(vertex_counts.size());
            if (style_vbufs.size() < vertex_counts.size())
                style_vbufs.resize(vertex_counts.size());

            for (std::size_t i = 0; i < vertex_counts.size(); ++i) {
                const std::size_t vertex_count = vertex_counts[i];
                if (vertex_count == 0)
                    continue;

                ensureDynamicBuf(rhi(),
                                 pos_vbufs[i],
                                 QRhiBuffer::VertexBuffer,
                                 vertex_count * vertex_size,
                                 initial_pos_bytes);
                ensureDynamicBuf(rhi(),
                                 style_vbufs[i],
                                 QRhiBuffer::VertexBuffer,
                                 vertex_count * sizeof(StyleIndex),
                                 kInitialStreamStyleBufferBytes);
            }

            trimBufferVector(pos_vbufs, vertex_counts.size());
            trimBufferVector(style_vbufs, vertex_counts.size());
        };
        auto uploadPlannedStream = [&](std::vector<std::unique_ptr<QRhiBuffer>>& pos_vbufs,
                                       std::vector<std::unique_ptr<QRhiBuffer>>& style_vbufs,
                                       const std::vector<PendingUpload>&         uploads) {
            for (const PendingUpload& upload : uploads) {
                const int pos_bytes   = int(std::size_t(upload.count) * upload.pos_vertex_size);
                const int style_bytes = int(std::size_t(upload.count) * sizeof(StyleIndex));
                u->updateDynamicBuffer(pos_vbufs[upload.buffer_index].get(),
                                       int(upload.pos_offset),
                                       pos_bytes,
                                       upload.pos_data);
                u->updateDynamicBuffer(style_vbufs[upload.buffer_index].get(),
                                       int(upload.style_offset),
                                       style_bytes,
                                       upload.style_data);
            }
        };
        ensureBufferSet(frame.line_vbufs,       frame.line_style_vbufs,       line_buffer_vertices,
                        sizeof(PosVertex),       kInitialStreamVertexBufferBytes);
        ensureBufferSet(frame.fill_vbufs,       frame.fill_style_vbufs,       fill_buffer_vertices,
                        sizeof(PosVertex),       kInitialStreamVertexBufferBytes);
        ensureBufferSet(frame.draw_vbufs,       frame.draw_style_vbufs,       draw_buffer_vertices,
                        sizeof(PosVertex),       kInitialStreamVertexBufferBytes);
        ensureBufferSet(frame.thick_line_instance_vbufs, frame.thick_line_style_vbufs,
                        thick_line_buffer_vertices,
                        sizeof(ThickLineInstance), kInitialThickInstanceBufferBytes);
        ensureBufferSet(frame.dashed_line_instance_vbufs, frame.dashed_line_style_vbufs,
                        dashed_line_buffer_vertices,
                        sizeof(DashedLineInstance), kInitialDashedInstanceBufferBytes);
        uploadPlannedStream(frame.line_vbufs,       frame.line_style_vbufs,       line_uploads);
        uploadPlannedStream(frame.fill_vbufs,       frame.fill_style_vbufs,       fill_uploads);
        uploadPlannedStream(frame.draw_vbufs,       frame.draw_style_vbufs,       draw_uploads);
        uploadPlannedStream(frame.thick_line_instance_vbufs,  frame.thick_line_style_vbufs,  thick_line_uploads);
        uploadPlannedStream(frame.dashed_line_instance_vbufs, frame.dashed_line_style_vbufs, dashed_line_uploads);

        PaletteUniformBlock palette_data{};
        const std::size_t palette_count =
            std::min<std::size_t>(palette_rgba.size(), kMaxRhiStyleEntries);
        for (std::size_t i = 0; i < palette_count; ++i)
            palette_data.colors[i] = makeColorUniform(palette_rgba[i]);
        u->updateDynamicBuffer(frame.palette_ubuf.get(),
                               0,
                               sizeof(PaletteUniformBlock),
                               &palette_data);
    }
    // Camera-only frame: tiled vertex/style buffers and palette are reused.

    // --- Record draw commands ------------------------------------------------
    cb->beginPass(renderTarget(), bg, { 1.0f, 0 }, u);
    cb->setViewport(QRhiViewport(0, 0, float(width()), float(height())));

    auto drawChunks = [&](std::unique_ptr<QRhiGraphicsPipeline>& pso,
                          std::vector<std::unique_ptr<QRhiBuffer>>& pos_vbufs,
                          std::vector<std::unique_ptr<QRhiBuffer>>& style_vbufs,
                          const std::vector<StreamChunk>&        chunks) {
        for (const StreamChunk& chunk : chunks) {
            if (chunk.count == 0)
                continue;

            cb->setGraphicsPipeline(pso.get());
            cb->setShaderResources(frame.srb.get());
            const QRhiCommandBuffer::VertexInput inputs[] = {
                { pos_vbufs[chunk.buffer_index].get(), chunk.pos_offset },
                { style_vbufs[chunk.buffer_index].get(), chunk.style_offset }
            };
            cb->setVertexInput(0, 2, inputs);
            cb->draw(chunk.count);
        }
    };

    std::size_t visible_tile_count = 0;
    for (const GpuTileBatch& tile : frame.gpu_tiles) {
        if (!rectanglesIntersect(tile.world_bounds, visible_world))
            continue;

        ++visible_tile_count;
        // Draw order: fills first (bottom), then outlines and lines on top.
        // This matches painter semantics — the last-submitted primitive type
        // appears on top, so fills (submitted first) are always below lines.
        drawChunks(m_fill_pso,       frame.fill_vbufs,       frame.fill_style_vbufs,       tile.fill_chunks);
        drawChunks(m_draw_pso,       frame.draw_vbufs,       frame.draw_style_vbufs,       tile.draw_chunks);
        drawChunks(m_line_pso,       frame.line_vbufs,       frame.line_style_vbufs,       tile.line_chunks);
        // Dashed lines: instanced draw — 4 quad-corner vertices × N instances.
        // Uses a dedicated fragment shader that discards gap fragments.
        for (const StreamChunk& chunk : tile.dashed_line_chunks) {
            if (chunk.count == 0)
                continue;
            cb->setGraphicsPipeline(m_dashed_line_pso.get());
            cb->setShaderResources(frame.srb.get());
            const QRhiCommandBuffer::VertexInput inputs[3] = {
                { m_thick_line_corner_vbuf.get(), 0 },
                { frame.dashed_line_instance_vbufs[chunk.buffer_index].get(), chunk.pos_offset },
                { frame.dashed_line_style_vbufs[chunk.buffer_index].get(),    chunk.style_offset }
            };
            cb->setVertexInput(0, 3, inputs);
            cb->draw(4, chunk.count);
        }

        // Thick solid lines: instanced draw — 4 quad-corner vertices × N instances.
        for (const StreamChunk& chunk : tile.thick_line_chunks) {
            if (chunk.count == 0)
                continue;
            cb->setGraphicsPipeline(m_thick_line_pso.get());
            cb->setShaderResources(frame.srb.get());
            const QRhiCommandBuffer::VertexInput inputs[3] = {
                { m_thick_line_corner_vbuf.get(), 0 },
                { frame.thick_line_instance_vbufs[chunk.buffer_index].get(), chunk.pos_offset },
                { frame.thick_line_style_vbufs[chunk.buffer_index].get(),    chunk.style_offset }
            };
            cb->setVertexInput(0, 3, inputs);
            cb->draw(4, chunk.count); // 4 quad corners, chunk.count instances
        }
    }

    cb->endPass();

    const auto frame_end = std::chrono::steady_clock::now();
    const double frame_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
    auto countChunkVertices = [](const std::vector<StreamChunk>& chunks) {
        unsigned long long total = 0;
        for (const StreamChunk& chunk : chunks)
            total += chunk.count;
        return total;
    };
    unsigned long long line_verts       = 0;
    unsigned long long fill_verts       = 0;
    unsigned long long draw_verts       = 0;
    unsigned long long thick_line_verts = 0;
    unsigned long long dashed_line_verts = 0;
    for (const GpuTileBatch& tile : frame.gpu_tiles) {
        line_verts       += countChunkVertices(tile.line_chunks);
        fill_verts       += countChunkVertices(tile.fill_chunks);
        draw_verts       += countChunkVertices(tile.draw_chunks);
        thick_line_verts += countChunkVertices(tile.thick_line_chunks); // = instance count
        dashed_line_verts += countChunkVertices(tile.dashed_line_chunks); // = instance count
    }
    g_debug("RHI render() CPU time %.3f ms (frame_slot=%d, geom_dirty=%d, tiles=%zu, visible_tiles=%zu, "
            "line_verts=%llu, fill_verts=%llu, draw_verts=%llu, thick_line_verts=%llu, "
            "dashed_line_verts=%llu)",
            frame_ms,
            frame_slot,
            int(geom_dirty),
            frame.gpu_tiles.size(),
            visible_tile_count,
            line_verts,
            fill_verts,
            draw_verts,
            thick_line_verts,
            dashed_line_verts);
}

void RhiCanvasWidget::releaseResources()
{
    auto resetBufferVector = [](std::vector<std::unique_ptr<QRhiBuffer>>& buffers) {
        buffers.clear();
    };

    m_dashed_line_pso.reset();
    m_thick_line_pso.reset();
    m_draw_pso.reset();
    m_fill_pso.reset();
    m_line_pso.reset();
    m_thick_line_corner_vbuf.reset();
    for (FrameResources& frame : m_frame_resources) {
        frame.srb.reset();
        resetBufferVector(frame.dashed_line_style_vbufs);
        resetBufferVector(frame.dashed_line_instance_vbufs);
        resetBufferVector(frame.thick_line_style_vbufs);
        resetBufferVector(frame.thick_line_instance_vbufs);
        resetBufferVector(frame.draw_style_vbufs);
        resetBufferVector(frame.draw_vbufs);
        resetBufferVector(frame.fill_style_vbufs);
        resetBufferVector(frame.fill_vbufs);
        resetBufferVector(frame.line_style_vbufs);
        resetBufferVector(frame.line_vbufs);
        frame.gpu_tiles.clear();
        frame.palette_ubuf.reset();
        frame.mvp_ubuf.reset();
    }
    m_frame_resources.clear();
    m_initialized = false;
}

void RhiCanvasWidget::paintEvent(QPaintEvent* e)
{
    // QRhiWidget::paintEvent renders the GPU texture to the widget surface.
    QRhiWidget::paintEvent(e);

#ifndef SKIP_LEGACY_PAINTER_OVERLAY
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
#endif
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
