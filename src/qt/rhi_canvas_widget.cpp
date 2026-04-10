#if defined(EZGL_QT) && defined(EZGL_RHI)

#include "ezgl/qt/rhi_canvas_widget.hpp"

#include <algorithm>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <QResizeEvent>
#include <QShowEvent>
#include <QFile>
#include <QMutexLocker>
#include <chrono>
#include <cmath>
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
constexpr std::uint32_t kInvalidDenseTileIndex =
    std::numeric_limits<std::uint32_t>::max();

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

int clampTileCoord(double value, double origin, double tile_extent, int tile_count)
{
    if (tile_count <= 0)
        return 0;

    const double normalized = (value - origin) / tile_extent;
    return std::clamp(int(std::floor(normalized)), 0, tile_count - 1);
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

struct OverlayVertex {
    float x;
    float y;
    float u;
    float v;
};
static_assert(sizeof(OverlayVertex) == 16,
              "OverlayVertex must be 16 bytes");

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

void buildOverlayPipeline(QRhi*                                   rhi,
                          std::unique_ptr<QRhiGraphicsPipeline>&  pso,
                          const QShader&                           overlay_vs,
                          const QShader&                           overlay_fs,
                          QRhiShaderResourceBindings*              srb,
                          QRhiRenderPassDescriptor*                rpDesc)
{
    QRhiVertexInputLayout layout;
    layout.setBindings({
        QRhiVertexInputBinding(sizeof(OverlayVertex))
    });
    layout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2,
                                 offsetof(OverlayVertex, x)),
        QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float2,
                                 offsetof(OverlayVertex, u))
    });

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable   = true;
    // The overlay QImage is premultiplied-alpha, so use premultiplied blending.
    blend.srcColor = QRhiGraphicsPipeline::One;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    pso.reset(rhi->newGraphicsPipeline());
    pso->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    pso->setVertexInputLayout(layout);
    pso->setShaderStages({
        { QRhiShaderStage::Vertex,   overlay_vs },
        { QRhiShaderStage::Fragment, overlay_fs }
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
                                     const RhiTileGridInfo&     tile_grid,
                                     const QMatrix4x4&          world_to_ndc,
                                     const rectangle&           visible_world,
                                     const QImage&              overlay,
                                     QColor                     bg_color)
{
    QMutexLocker lock(&m_frame_mutex);
    auto tiles_ptr = std::make_shared<const std::vector<RhiTileBatch>>(std::move(tiles));
    auto palette_ptr = std::make_shared<const std::vector<std::uint32_t>>(std::move(palette_rgba));
    m_pending_tiles        = tiles_ptr;
    m_pending_palette_rgba = palette_ptr;
    m_cached_tiles         = std::move(tiles_ptr);
    m_cached_palette_rgba  = std::move(palette_ptr);
    m_pending_tile_grid    = tile_grid;
    m_cached_tile_grid     = tile_grid;
    m_pending_mvp          = world_to_ndc;
    m_pending_visible_world = visible_world;
    m_pending_overlay      = overlay;
    m_pending_bg           = bg_color;
    m_frame_dirty          = true;
    m_mvp_dirty            = false;  // superseded by full frame
    std::fill(m_frame_slot_geom_valid.begin(), m_frame_slot_geom_valid.end(), false);
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

void RhiCanvasWidget::set_mvp_and_overlay(const QMatrix4x4& world_to_ndc,
                                          const rectangle&  visible_world,
                                          const QImage&     overlay)
{
    QMutexLocker lock(&m_frame_mutex);
    m_pending_mvp = world_to_ndc;
    m_pending_visible_world = visible_world;
    m_pending_overlay = overlay;
    m_mvp_dirty = true;
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
    QShader overlay_vs  = loadShader(":/ezgl/overlay.vert.qsb");
    QShader overlay_fs  = loadShader(":/ezgl/overlay.frag.qsb");

    m_frame_resources.clear();
    m_frame_resources.resize(std::max(1, rhi()->resourceLimit(QRhi::FramesInFlight)));
    m_frame_slot_geom_valid.assign(m_frame_resources.size(), false);

    m_overlay_sampler.reset(
        rhi()->newSampler(QRhiSampler::Nearest,
                          QRhiSampler::Nearest,
                          QRhiSampler::None,
                          QRhiSampler::ClampToEdge,
                          QRhiSampler::ClampToEdge));
    m_overlay_sampler->create();

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
        frame.overlay_tex.reset(rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        frame.overlay_tex->create();
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

    m_overlay_quad_vbuf.reset(
        rhi()->newBuffer(QRhiBuffer::Dynamic,
                         QRhiBuffer::VertexBuffer,
                         int(4 * sizeof(OverlayVertex))));
    m_overlay_quad_vbuf->create();

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

        frame.overlay_srb.reset(rhi()->newShaderResourceBindings());
        frame.overlay_srb->setBindings({
            QRhiShaderResourceBinding::sampledTexture(
                0,
                QRhiShaderResourceBinding::FragmentStage,
                frame.overlay_tex.get(),
                m_overlay_sampler.get())
        });
        frame.overlay_srb->create();
    }

    auto* rpDesc = renderTarget()->renderPassDescriptor();
    QRhiShaderResourceBindings* const pipeline_srb = m_frame_resources.front().srb.get();
    QRhiShaderResourceBindings* const overlay_srb = m_frame_resources.front().overlay_srb.get();
    buildPipeline(rhi(), m_line_pso,  QRhiGraphicsPipeline::Lines,     vs, fs, pipeline_srb, rpDesc);
    buildPipeline(rhi(), m_fill_pso,  QRhiGraphicsPipeline::Triangles, vs, fs, pipeline_srb, rpDesc);
    buildPipeline(rhi(), m_draw_pso,  QRhiGraphicsPipeline::Lines,     vs, fs, pipeline_srb, rpDesc);
    buildThickLinePipeline(rhi(), m_thick_line_pso, thick_vs, fs, pipeline_srb, rpDesc);
    buildDashedLinePipeline(rhi(), m_dashed_line_pso, dashed_vs, dashed_fs, pipeline_srb, rpDesc);
    buildOverlayPipeline(rhi(), m_overlay_pso, overlay_vs, overlay_fs, overlay_srb, rpDesc);

    m_initialized = true;
}

void RhiCanvasWidget::render(QRhiCommandBuffer* cb)
{
    if (!m_initialized || m_frame_resources.empty())
        return;

    const auto frame_start = std::chrono::steady_clock::now();
    const int frame_slot = currentFrameResourceIndex(rhi(), m_frame_resources.size());

    // --- Snapshot pending frame under lock -----------------------------------
    std::shared_ptr<const std::vector<RhiTileBatch>> tiles;
    std::shared_ptr<const std::vector<std::uint32_t>> palette_rgba;
    QMatrix4x4 mvp;
    rectangle  visible_world;
    QImage     overlay;
    QColor     bg;
    RhiTileGridInfo tile_grid;
    bool       geom_dirty;

    {
        QMutexLocker lock(&m_frame_mutex);
        const bool need_geom_for_slot =
            std::size_t(frame_slot) >= m_frame_slot_geom_valid.size()
            || (!m_frame_slot_geom_valid[std::size_t(frame_slot)] && m_cached_tiles);

        if (!m_frame_dirty && !m_mvp_dirty && !need_geom_for_slot)
            return;

        geom_dirty = m_frame_dirty || need_geom_for_slot;
        mvp        = m_pending_mvp;
        visible_world = m_pending_visible_world;
        overlay    = m_pending_overlay;
        bg         = m_pending_bg;
        tile_grid  = m_cached_tile_grid;

        if (geom_dirty) {
            if (m_frame_dirty && m_pending_tiles) {
                tiles        = m_pending_tiles;
                palette_rgba = m_pending_palette_rgba;
            } else {
                tiles        = m_cached_tiles;
                palette_rgba = m_cached_palette_rgba;
            }
        }
        m_frame_dirty = false;
        m_mvp_dirty   = false;
        m_pending_tiles.reset();
        m_pending_palette_rgba.reset();
        // m_pending_overlay stays cached here; render() may re-upload it on
        // subsequent camera-only updates.
    }

    FrameResources& frame = m_frame_resources[std::size_t(frame_slot)];
    const bool has_overlay = !overlay.isNull();
    if (has_overlay) {
        overlay = overlay.convertToFormat(QImage::Format_RGBA8888_Premultiplied);
    }

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
    {
        static const OverlayVertex kOverlayQuad[4] = {
            { -1.0f, +1.0f, 0.0f, 0.0f }, // top-left
            { -1.0f, -1.0f, 0.0f, 1.0f }, // bottom-left
            { +1.0f, +1.0f, 1.0f, 0.0f }, // top-right
            { +1.0f, -1.0f, 1.0f, 1.0f }  // bottom-right
        };
        u->updateDynamicBuffer(m_overlay_quad_vbuf.get(), 0,
                               int(sizeof(kOverlayQuad)), kOverlayQuad);
    }

    if (has_overlay) {
        const QSize overlay_size = overlay.size();
        if (!frame.overlay_tex || frame.overlay_tex->pixelSize() != overlay_size) {
            frame.overlay_srb.reset();
            frame.overlay_tex.reset(rhi()->newTexture(QRhiTexture::RGBA8, overlay_size));
            frame.overlay_tex->create();
            frame.overlay_srb.reset(rhi()->newShaderResourceBindings());
            frame.overlay_srb->setBindings({
                QRhiShaderResourceBinding::sampledTexture(
                    0,
                    QRhiShaderResourceBinding::FragmentStage,
                    frame.overlay_tex.get(),
                    m_overlay_sampler.get())
            });
            frame.overlay_srb->create();
        }
        u->uploadTexture(frame.overlay_tex.get(), overlay);
    }

    if (geom_dirty) {
        if (!tiles || !palette_rgba) {
            qFatal("RhiCanvasWidget: geom_dirty set without cached tile/palette data");
        }

        if (palette_rgba->size() > kMaxRhiStyleEntries) {
            qFatal("RhiCanvasWidget: palette size %zu exceeds limit %zu",
                   palette_rgba->size(), kMaxRhiStyleEntries);
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
        for (const RhiTileBatch& tile : *tiles) {
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

        frame.gpu_tiles.resize(tiles->size());
        frame.tile_grid = tile_grid;
        const std::size_t dense_tile_count =
            std::size_t(tile_grid.cols) * std::size_t(tile_grid.rows);
        frame.dense_tile_lookup.assign(dense_tile_count, kInvalidDenseTileIndex);

        for (std::size_t i = 0; i < tiles->size(); ++i) {
            const RhiTileBatch& tile     = (*tiles)[i];
            GpuTileBatch&       gpu_tile = frame.gpu_tiles[i];
            gpu_tile.world_bounds = tile.world_bounds;
            gpu_tile.tile_x = tile.tile_x;
            gpu_tile.tile_y = tile.tile_y;
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

            const std::size_t dense_index =
                std::size_t(tile.tile_y) * std::size_t(tile_grid.cols) + std::size_t(tile.tile_x);
            if (dense_index >= frame.dense_tile_lookup.size()) {
                qFatal("RhiCanvasWidget: tile lookup index %zu exceeds dense grid size %zu",
                       dense_index,
                       frame.dense_tile_lookup.size());
            }
            frame.dense_tile_lookup[dense_index] = std::uint32_t(i);
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
            std::min<std::size_t>(palette_rgba->size(), kMaxRhiStyleEntries);
        for (std::size_t i = 0; i < palette_count; ++i)
            palette_data.colors[i] = makeColorUniform((*palette_rgba)[i]);
        u->updateDynamicBuffer(frame.palette_ubuf.get(),
                               0,
                               sizeof(PaletteUniformBlock),
                               &palette_data);

        {
            QMutexLocker lock(&m_frame_mutex);
            if (std::size_t(frame_slot) >= m_frame_slot_geom_valid.size())
                m_frame_slot_geom_valid.resize(m_frame_resources.size(), false);
            m_frame_slot_geom_valid[std::size_t(frame_slot)] = true;
        }
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

    auto countChunkVertices = [](const std::vector<StreamChunk>& chunks) {
        unsigned long long total = 0;
        for (const StreamChunk& chunk : chunks)
            total += chunk.count;
        return total;
    };

    std::size_t visible_tile_count = 0;
    std::size_t considered_tile_count = 0;
    unsigned long long visible_line_verts        = 0;
    unsigned long long visible_fill_verts        = 0;
    unsigned long long visible_draw_verts        = 0;
    unsigned long long visible_thick_line_verts  = 0;
    unsigned long long visible_dashed_line_verts = 0;
    auto drawTile = [&](const GpuTileBatch& tile) {
        ++visible_tile_count;
        visible_fill_verts        += countChunkVertices(tile.fill_chunks);
        visible_draw_verts        += countChunkVertices(tile.draw_chunks);
        visible_line_verts        += countChunkVertices(tile.line_chunks);
        visible_thick_line_verts  += countChunkVertices(tile.thick_line_chunks);
        visible_dashed_line_verts += countChunkVertices(tile.dashed_line_chunks);
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
    };

    const bool can_direct_index_tiles =
        frame.tile_grid.cols > 0
        && frame.tile_grid.rows > 0
        && frame.dense_tile_lookup.size()
            == std::size_t(frame.tile_grid.cols) * std::size_t(frame.tile_grid.rows);
    if (can_direct_index_tiles
        && rectanglesIntersect(frame.tile_grid.scene_bounds, visible_world)) {
        const double tile_width =
            frame.tile_grid.scene_bounds.width() / double(frame.tile_grid.cols);
        const double tile_height =
            frame.tile_grid.scene_bounds.height() / double(frame.tile_grid.rows);
        const int min_tx =
            clampTileCoord(visible_world.left(),
                           frame.tile_grid.scene_bounds.left(),
                           tile_width,
                           int(frame.tile_grid.cols));
        const int max_tx =
            clampTileCoord(visible_world.right(),
                           frame.tile_grid.scene_bounds.left(),
                           tile_width,
                           int(frame.tile_grid.cols));
        const int min_ty =
            clampTileCoord(visible_world.bottom(),
                           frame.tile_grid.scene_bounds.bottom(),
                           tile_height,
                           int(frame.tile_grid.rows));
        const int max_ty =
            clampTileCoord(visible_world.top(),
                           frame.tile_grid.scene_bounds.bottom(),
                           tile_height,
                           int(frame.tile_grid.rows));

        for (int ty = min_ty; ty <= max_ty; ++ty) {
            for (int tx = min_tx; tx <= max_tx; ++tx) {
                ++considered_tile_count;
                const std::size_t dense_index =
                    std::size_t(ty) * std::size_t(frame.tile_grid.cols) + std::size_t(tx);
                const std::uint32_t gpu_index = frame.dense_tile_lookup[dense_index];
                if (gpu_index == kInvalidDenseTileIndex)
                    continue;

                drawTile(frame.gpu_tiles[std::size_t(gpu_index)]);
            }
        }
    } else {
        for (const GpuTileBatch& tile : frame.gpu_tiles) {
            ++considered_tile_count;
            if (!rectanglesIntersect(tile.world_bounds, visible_world))
                continue;
            drawTile(tile);
        }
    }

    if (has_overlay) {
        cb->setGraphicsPipeline(m_overlay_pso.get());
        cb->setShaderResources(frame.overlay_srb.get());
        const QRhiCommandBuffer::VertexInput input = {
            m_overlay_quad_vbuf.get(), 0
        };
        cb->setVertexInput(0, 1, &input);
        cb->draw(4);
    }

    cb->endPass();

    const auto frame_end = std::chrono::steady_clock::now();
    const double frame_ms = std::chrono::duration<double, std::milli>(frame_end - frame_start).count();
    g_debug("RHI render() CPU time %.3f ms (frame_slot=%d, geom_dirty=%d, tiles=%zu, considered_tiles=%zu, visible_tiles=%zu, "
            "line_verts=%llu, fill_verts=%llu, draw_verts=%llu, thick_line_verts=%llu, "
            "dashed_line_verts=%llu)",
            frame_ms,
            frame_slot,
            int(geom_dirty),
            frame.gpu_tiles.size(),
            considered_tile_count,
            visible_tile_count,
            visible_line_verts,
            visible_fill_verts,
            visible_draw_verts,
            visible_thick_line_verts,
            visible_dashed_line_verts);
}

void RhiCanvasWidget::releaseResources()
{
    auto resetBufferVector = [](std::vector<std::unique_ptr<QRhiBuffer>>& buffers) {
        buffers.clear();
    };

    m_overlay_pso.reset();
    m_dashed_line_pso.reset();
    m_thick_line_pso.reset();
    m_draw_pso.reset();
    m_fill_pso.reset();
    m_line_pso.reset();
    m_overlay_sampler.reset();
    m_overlay_quad_vbuf.reset();
    m_thick_line_corner_vbuf.reset();
    for (FrameResources& frame : m_frame_resources) {
        frame.overlay_srb.reset();
        frame.overlay_tex.reset();
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
    m_frame_slot_geom_valid.clear();
    m_initialized = false;
}

void RhiCanvasWidget::resizeEvent(QResizeEvent* e)
{
    if (m_pre_resize_cb)
        m_pre_resize_cb();
    QRhiWidget::resizeEvent(e);
    if (width() > 0 && height() > 0 && m_resize_cb)
        m_resize_cb(width(), height());
}

void RhiCanvasWidget::showEvent(QShowEvent* e)
{
    QRhiWidget::showEvent(e);
    if (width() > 0 && height() > 0 && m_resize_cb)
        m_resize_cb(width(), height());
}

} // namespace ezgl

#endif // EZGL_QT && EZGL_RHI
