#include "ezgl/qt/rhi_canvas_widget.hpp"
#include "ezgl/logutils.hpp"

#include <algorithm>
#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <QResizeEvent>
#include <QShowEvent>
#include <QFile>
#include <QMutexLocker>
#include <cmath>
#include <cstring>
#include <limits>


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
constexpr std::size_t kInitialThinLineBufferBytes        = 1 * 1024 * 1024;
constexpr std::size_t kInitialFillRectBufferBytes        = 512 * 1024;
constexpr std::size_t kInitialFillPolyBufferBytes        = 512 * 1024;
constexpr std::size_t kInitialThickInstanceBufferBytes   = 512 * 1024;
constexpr std::size_t kInitialDashedInstanceBufferBytes  = 512 * 1024;
constexpr std::size_t kInitialStyleUniformBufferBytes    = 16 * 1024;
constexpr std::size_t kMaxPosVerticesPerBuffer =
    kMaxQrhiBufferBytes / sizeof(ezgl::PosVertex);
constexpr std::size_t kMaxFillRectInstancesPerBuffer =
    kMaxQrhiBufferBytes / sizeof(ezgl::FillRectInstance);
constexpr std::size_t kMaxThickInstancesPerBuffer =
    kMaxQrhiBufferBytes / sizeof(ezgl::ThickLineInstance);
constexpr std::size_t kMaxDashedInstancesPerBuffer =
    kMaxQrhiBufferBytes / sizeof(ezgl::DashedLineInstance);

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

std::size_t alignUp(std::size_t value, std::size_t alignment)
{
    if (alignment == 0)
        return value;
    const std::size_t remainder = value % alignment;
    return remainder == 0 ? value : (value + alignment - remainder);
}

struct StyleUniform {
    float color[4];
    float line[4]; // x: width_px, y: dash_px, z: gap_px, w: unused
};
static_assert(sizeof(StyleUniform) == 32,
              "StyleUniform must match two std140 vec4 values");

struct OverlayVertex {
    float x;
    float y;
    float u;
    float v;
};
static_assert(sizeof(OverlayVertex) == 16,
              "OverlayVertex must be 16 bytes");

StyleUniform makeStyleUniform(ezgl::StyleKey style_key, std::uint32_t rgba)
{
    constexpr float kScale = 1.0f / 255.0f;
    float width_px = float(ezgl::style_key_line_width(style_key));
    if (width_px <= 0.0f)
        width_px = 1.0f;

    float dash_px = 0.0f;
    float gap_px = 0.0f;
    if (ezgl::style_key_line_dash(style_key) != 0) {
        dash_px = 5.0f * width_px;
        gap_px  = 3.0f * width_px;
    }

    return StyleUniform{{
        float((rgba >>  0) & 0xFF) * kScale,
        float((rgba >>  8) & 0xFF) * kScale,
        float((rgba >> 16) & 0xFF) * kScale,
        float((rgba >> 24) & 0xFF) * kScale
    }, {
        width_px,
        dash_px,
        gap_px,
        0.0f
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
        QRhiVertexInputBinding(sizeof(ezgl::PosVertex))
    });
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

void buildFillRectPipeline(QRhi*                                  rhi,
                           std::unique_ptr<QRhiGraphicsPipeline>& pso,
                           const QShader&                         vs,
                           const QShader&                         fs,
                           QRhiShaderResourceBindings*            srb,
                           QRhiRenderPassDescriptor*              rpDesc)
{
    QRhiVertexInputLayout layout;
    layout.setBindings({
        QRhiVertexInputBinding(sizeof(ezgl::FillRectInstance),
                               QRhiVertexInputBinding::PerInstance)
    });
    layout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::FillRectInstance, x0)),
        QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::FillRectInstance, x1))
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
 * Vertex format uses QuadCorner in binding 0 and ThickLineInstance in binding 1.
 * The vertex shader (thick_line.vert) expands each
 * vertex perpendicularly using the MVP + viewport uniforms plus width from
 * the dynamic style UBO. The fragment shader reads color from the same UBO.
 */
void buildThickLinePipeline(QRhi*                                   rhi,
                            std::unique_ptr<QRhiGraphicsPipeline>&  pso,
                            const QShader&                           thick_vs,
                            const QShader&                           fs,
                            QRhiShaderResourceBindings*              srb,
                            QRhiRenderPassDescriptor*                rpDesc)
{
    // Instanced rendering:
    //   Binding 0 (PerVertex)   — QuadCorner   (t, side) — 4 corners, constant
    //   Binding 1 (PerInstance) — ThickLineInstance (x0,y0,x1,y1)
    QRhiVertexInputLayout layout;
    layout.setBindings({
        QRhiVertexInputBinding(sizeof(ezgl::QuadCorner)),
        QRhiVertexInputBinding(sizeof(ezgl::ThickLineInstance),
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
                                 offsetof(ezgl::ThickLineInstance, x1))
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
    // Binding 1 (PerInstance) — DashedLineInstance (x0,y0,x1,y1,phase_world)
    QRhiVertexInputLayout layout;
    layout.setBindings({
        QRhiVertexInputBinding(sizeof(ezgl::QuadCorner)),
        QRhiVertexInputBinding(sizeof(ezgl::DashedLineInstance),
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
                                 offsetof(ezgl::DashedLineInstance, phase_world))
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

void RhiCanvasWidget::set_frame_data(SceneBuffers       scene_buffers,
                                     const QMatrix4x4& world_to_ndc,
                                     const rectangle&  visible_world,
                                     const QImage&     overlay,
                                     QColor            bg_color)
{
    QMutexLocker lock(&m_frame_mutex);
    auto scene_ptr = std::make_shared<const SceneBuffers>(std::move(scene_buffers));
    m_pending_scene_buffers = scene_ptr;
    m_cached_scene_buffers = std::move(scene_ptr);
    m_pending_mvp = world_to_ndc;
    m_pending_visible_world = visible_world;
    m_pending_overlay = overlay;
    m_pending_bg = bg_color;
    m_frame_dirty = true;
    m_mvp_dirty = false;  // superseded by full frame
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

// ---- QRhiWidget overrides --------------------------------------------------

void RhiCanvasWidget::initialize(QRhiCommandBuffer* /*cb*/)
{
    QShader line_vs      = loadShader(":/ezgl/line.vert.qsb");
    QShader line_fs      = loadShader(":/ezgl/line.frag.qsb");
    QShader fill_rect_vs = loadShader(":/ezgl/fill_rect.vert.qsb");
    QShader thick_vs     = loadShader(":/ezgl/thick_line.vert.qsb");
    QShader dashed_vs    = loadShader(":/ezgl/dashed_line.vert.qsb");
    QShader dashed_fs    = loadShader(":/ezgl/dashed_line.frag.qsb");
    QShader overlay_vs   = loadShader(":/ezgl/overlay.vert.qsb");
    QShader overlay_fs   = loadShader(":/ezgl/overlay.frag.qsb");

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
        frame.style_ubuf.reset(rhi()->newBuffer(QRhiBuffer::Dynamic,
                                                QRhiBuffer::UniformBuffer,
                                                int(std::max<std::size_t>(
                                                    kInitialStyleUniformBufferBytes,
                                                    std::size_t(rhi()->ubufAlignment())))));
        frame.style_ubuf->create();
        frame.thin_line_vbufs.clear();
        frame.fill_rect_instance_vbufs.clear();
        frame.fill_poly_vbufs.clear();
        frame.thick_line_instance_vbufs.clear();
        frame.dashed_line_instance_vbufs.clear();
        frame.overlay_tex.reset(rhi()->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        frame.overlay_tex->create();
        frame.gpu_scene.clear();
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

    // Shader resource bindings:
    //   srb      — MVP at binding 0, per-style color UBO at binding 1 via dynamic offsets
    // All pipelines get per-frame concrete buffers to avoid cross-frame aliasing.
    for (FrameResources& frame : m_frame_resources) {
        frame.srb.reset(rhi()->newShaderResourceBindings());
        frame.srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0,
                QRhiShaderResourceBinding::VertexStage,
                frame.mvp_ubuf.get()),
            QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                1,
                QRhiShaderResourceBinding::VertexStage
                    | QRhiShaderResourceBinding::FragmentStage,
                frame.style_ubuf.get(),
                sizeof(StyleUniform))
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
    buildPipeline(rhi(), m_line_pso, QRhiGraphicsPipeline::Lines, line_vs, line_fs, pipeline_srb, rpDesc);
    buildFillRectPipeline(rhi(), m_fill_rect_pso, fill_rect_vs, line_fs, pipeline_srb, rpDesc);
    buildPipeline(rhi(), m_fill_poly_pso, QRhiGraphicsPipeline::Triangles, line_vs, line_fs, pipeline_srb, rpDesc);
    buildThickLinePipeline(rhi(), m_thick_line_pso, thick_vs, line_fs, pipeline_srb, rpDesc);
    buildDashedLinePipeline(rhi(), m_dashed_line_pso, dashed_vs, dashed_fs, pipeline_srb, rpDesc);
    buildOverlayPipeline(rhi(), m_overlay_pso, overlay_vs, overlay_fs, overlay_srb, rpDesc);

    m_initialized = true;
}

void RhiCanvasWidget::render(QRhiCommandBuffer* cb)
{
    if (!m_initialized || m_frame_resources.empty())
        return;

#ifdef EZGL_RENDERER_DEBUG
    const scope_timer frame_timer;
#endif
    const int frame_slot = currentFrameResourceIndex(rhi(), m_frame_resources.size());

    // --- Snapshot pending frame under lock -----------------------------------
    std::shared_ptr<const SceneBuffers> scene_buffers;
    QMatrix4x4 mvp;
    rectangle  visible_world;
    QImage     overlay;
    QColor     bg;
    bool       geom_dirty;

    {
        QMutexLocker lock(&m_frame_mutex);
        const bool need_geom_for_slot =
            std::size_t(frame_slot) >= m_frame_slot_geom_valid.size()
            || (!m_frame_slot_geom_valid[std::size_t(frame_slot)] && m_cached_scene_buffers);

        if (!m_frame_dirty && !m_mvp_dirty && !need_geom_for_slot)
            return;

        geom_dirty = m_frame_dirty || need_geom_for_slot;
        mvp        = m_pending_mvp;
        visible_world = m_pending_visible_world;
        overlay    = m_pending_overlay;
        bg         = m_pending_bg;

        if (geom_dirty) {
            if (m_frame_dirty && m_pending_scene_buffers) {
                scene_buffers = m_pending_scene_buffers;
            } else {
                scene_buffers = m_cached_scene_buffers;
            }
        }
        m_frame_dirty = false;
        m_mvp_dirty   = false;
        m_pending_scene_buffers.reset();
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
        // Use device-pixel size for the viewport uniform so that thick-line
        // width calculations match the actual framebuffer resolution (HiDPI).
        const QSize ps = renderTarget()->pixelSize();
        const float vp[2] = { float(ps.width()), float(ps.height()) };
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
        if (!scene_buffers) {
            qFatal("RhiCanvasWidget: geom_dirty set without cached scene data");
        }
#ifdef EZGL_RENDERER_DEBUG
        const scope_timer geometry_bake_timer("bake geometry");
#endif
        struct PendingUpload {
            quint32     buffer_index = 0;
            quint32     byte_offset = 0;
            quint32     byte_size = 0;
            const void* data = nullptr;
        };

        const std::size_t style_stride =
            alignUp(sizeof(StyleUniform), std::size_t(rhi()->ubufAlignment()));
        const std::size_t total_style_count =
            scene_buffers->thin_lines.size()
            + scene_buffers->fill_rects.size()
            + scene_buffers->fill_polys.size()
            + scene_buffers->thick_lines.size()
            + scene_buffers->dashed_lines.size();
        std::vector<std::uint8_t> style_uniform_bytes(total_style_count * style_stride, 0);
        std::size_t next_style_index = 0;

        auto assign_style_offset = [&](ezgl::StyleKey style_key, std::uint32_t rgba) {
            const std::size_t offset = next_style_index * style_stride;
            const StyleUniform style = makeStyleUniform(style_key, rgba);
            std::memcpy(style_uniform_bytes.data() + offset, &style, sizeof(StyleUniform));
            ++next_style_index;
            return quint32(offset);
        };

        std::vector<std::size_t> thin_line_buffer_counts;
        std::vector<std::size_t> fill_rect_buffer_counts;
        std::vector<std::size_t> fill_poly_buffer_counts;
        std::vector<std::size_t> thick_line_buffer_counts;
        std::vector<std::size_t> dashed_line_buffer_counts;
        std::vector<PendingUpload> thin_line_uploads;
        std::vector<PendingUpload> fill_rect_uploads;
        std::vector<PendingUpload> fill_poly_uploads;
        std::vector<PendingUpload> thick_line_uploads;
        std::vector<PendingUpload> dashed_line_uploads;
        frame.gpu_scene.clear();

        auto planStyleBuffers = [&](const auto& scene_map,
                                    auto&       gpu_buffers,
                                    auto&       uploads,
                                    auto&       buffer_counts,
                                    std::size_t elem_size,
                                    std::size_t max_per_buffer,
                                    auto        get_data) {
            for (const auto& [style_key, scene_buffer] : scene_map) {
                const auto& data = get_data(scene_buffer);
                if (data.empty())
                    continue;

                GpuStyleBuffer gpu_buffer;
                gpu_buffer.style_key = style_key;
                gpu_buffer.rgba = scene_buffer.rgba;
                gpu_buffer.style_offset = assign_style_offset(style_key, scene_buffer.rgba);

                for (const Chunk& chunk : scene_buffer.chunks) {
                    std::size_t remaining = chunk.count;
                    std::size_t data_offset = chunk.offset;
                    while (remaining > 0) {
                        if (buffer_counts.empty() || buffer_counts.back() == max_per_buffer)
                            buffer_counts.push_back(0);

                        const std::size_t buffer_index = buffer_counts.size() - 1;
                        const std::size_t buffer_offset = buffer_counts.back();
                        const std::size_t count =
                            std::min(remaining, max_per_buffer - buffer_offset);
                        const std::size_t byte_offset = buffer_offset * elem_size;
                        const std::size_t byte_size = count * elem_size;
                        if (byte_offset > kMaxQrhiBufferBytes || byte_size > kMaxQrhiBufferBytes) {
                            qFatal("RhiCanvasWidget: planned buffer upload exceeds QRhi int-sized limit");
                        }

                        gpu_buffer.chunks.emplace_back(
                            chunk.world_bounds,
                            quint32(buffer_index),
                            quint32(byte_offset),
                            quint32(count)
                        );
                        uploads.emplace_back(
                            quint32(buffer_index),
                            quint32(byte_offset),
                            quint32(byte_size),
                            static_cast<const void*>(data.data() + data_offset)
                        );
                        buffer_counts.back() += count;
                        remaining -= count;
                        data_offset += count;
                    }
                }

                gpu_buffers.push_back(std::move(gpu_buffer));
            }
        };

        planStyleBuffers(scene_buffers->thin_lines,
                         frame.gpu_scene.thin_lines,
                         thin_line_uploads,
                         thin_line_buffer_counts,
                         sizeof(PosVertex),
                         kMaxPosVerticesPerBuffer,
                         [](const ThinLineStyleBuffer& buffer) -> const auto& { return buffer.verts; });
        planStyleBuffers(scene_buffers->fill_rects,
                         frame.gpu_scene.fill_rects,
                         fill_rect_uploads,
                         fill_rect_buffer_counts,
                         sizeof(FillRectInstance),
                         kMaxFillRectInstancesPerBuffer,
                         [](const FillRectStyleBuffer& buffer) -> const auto& { return buffer.instances; });
        planStyleBuffers(scene_buffers->fill_polys,
                         frame.gpu_scene.fill_polys,
                         fill_poly_uploads,
                         fill_poly_buffer_counts,
                         sizeof(PosVertex),
                         kMaxPosVerticesPerBuffer,
                         [](const FillPolyStyleBuffer& buffer) -> const auto& { return buffer.verts; });
        planStyleBuffers(scene_buffers->thick_lines,
                         frame.gpu_scene.thick_lines,
                         thick_line_uploads,
                         thick_line_buffer_counts,
                         sizeof(ThickLineInstance),
                         kMaxThickInstancesPerBuffer,
                         [](const ThickLineStyleBuffer& buffer) -> const auto& { return buffer.instances; });
        planStyleBuffers(scene_buffers->dashed_lines,
                         frame.gpu_scene.dashed_lines,
                         dashed_line_uploads,
                         dashed_line_buffer_counts,
                         sizeof(DashedLineInstance),
                         kMaxDashedInstancesPerBuffer,
                         [](const DashedLineStyleBuffer& buffer) -> const auto& { return buffer.instances; });

        auto trimBufferVector = [](std::vector<std::unique_ptr<QRhiBuffer>>& buffers,
                                   std::size_t                               keep_count) {
            for (std::size_t i = keep_count; i < buffers.size(); ++i)
                releaseDynamicBuf(buffers[i]);
            buffers.resize(keep_count);
        };
        auto ensureBufferPool = [&](std::vector<std::unique_ptr<QRhiBuffer>>& buffers,
                                    const std::vector<std::size_t>&           elem_counts,
                                    std::size_t                               elem_size,
                                    std::size_t                               initial_bytes) {
            if (buffers.size() < elem_counts.size())
                buffers.resize(elem_counts.size());

            for (std::size_t i = 0; i < elem_counts.size(); ++i) {
                const std::size_t elem_count = elem_counts[i];
                if (elem_count == 0)
                    continue;

                ensureDynamicBuf(rhi(),
                                 buffers[i],
                                 QRhiBuffer::VertexBuffer,
                                 elem_count * elem_size,
                                 initial_bytes);
            }

            trimBufferVector(buffers, elem_counts.size());
        };
        auto uploadPlannedStream = [&](std::vector<std::unique_ptr<QRhiBuffer>>& buffers,
                                       const std::vector<PendingUpload>&         uploads) {
            for (const PendingUpload& upload : uploads) {
                u->updateDynamicBuffer(buffers[upload.buffer_index].get(),
                                       int(upload.byte_offset),
                                       int(upload.byte_size),
                                       upload.data);
            }
        };

        const std::size_t style_ubuf_bytes =
            std::max(style_stride, style_uniform_bytes.empty() ? std::size_t(0) : style_uniform_bytes.size());
        ensureDynamicBuf(rhi(),
                         frame.style_ubuf,
                         QRhiBuffer::UniformBuffer,
                         style_ubuf_bytes,
                         std::max<std::size_t>(kInitialStyleUniformBufferBytes, style_stride));
        if (!style_uniform_bytes.empty()) {
            u->updateDynamicBuffer(frame.style_ubuf.get(),
                                   0,
                                   int(style_uniform_bytes.size()),
                                   style_uniform_bytes.data());
        }

        ensureBufferPool(frame.thin_line_vbufs, thin_line_buffer_counts,
                         sizeof(PosVertex), kInitialThinLineBufferBytes);
        ensureBufferPool(frame.fill_rect_instance_vbufs, fill_rect_buffer_counts,
                         sizeof(FillRectInstance), kInitialFillRectBufferBytes);
        ensureBufferPool(frame.fill_poly_vbufs, fill_poly_buffer_counts,
                         sizeof(PosVertex), kInitialFillPolyBufferBytes);
        ensureBufferPool(frame.thick_line_instance_vbufs, thick_line_buffer_counts,
                         sizeof(ThickLineInstance), kInitialThickInstanceBufferBytes);
        ensureBufferPool(frame.dashed_line_instance_vbufs, dashed_line_buffer_counts,
                         sizeof(DashedLineInstance), kInitialDashedInstanceBufferBytes);

        uploadPlannedStream(frame.thin_line_vbufs, thin_line_uploads);
        uploadPlannedStream(frame.fill_rect_instance_vbufs, fill_rect_uploads);
        uploadPlannedStream(frame.fill_poly_vbufs, fill_poly_uploads);
        uploadPlannedStream(frame.thick_line_instance_vbufs, thick_line_uploads);
        uploadPlannedStream(frame.dashed_line_instance_vbufs, dashed_line_uploads);

        {
            QMutexLocker lock(&m_frame_mutex);
            if (std::size_t(frame_slot) >= m_frame_slot_geom_valid.size())
                m_frame_slot_geom_valid.resize(m_frame_resources.size(), false);
            m_frame_slot_geom_valid[std::size_t(frame_slot)] = true;
        }
    }
    // Camera-only frame: geometry pools and style UBO are reused.

    // --- Record draw commands ------------------------------------------------
    cb->beginPass(renderTarget(), bg, { 1.0f, 0 }, u);
    // Use device-pixel size for the viewport so rendering fills the entire
    // framebuffer on HiDPI / Retina displays (DPR > 1).
    const QSize pixel_size = renderTarget()->pixelSize();
    cb->setViewport(QRhiViewport(0, 0, float(pixel_size.width()), float(pixel_size.height())));

    const bool mvp_only_frame = !geom_dirty;
    const std::size_t total_line_buffer_sets        = frame.thin_line_vbufs.size();
    const std::size_t total_fill_rect_buffer_sets   = frame.fill_rect_instance_vbufs.size();
    const std::size_t total_fill_poly_buffer_sets   = frame.fill_poly_vbufs.size();
    const std::size_t total_thick_line_buffer_sets  = frame.thick_line_instance_vbufs.size();
    const std::size_t total_dashed_line_buffer_sets = frame.dashed_line_instance_vbufs.size();
    const std::size_t total_stream_buffer_sets =
        total_line_buffer_sets
        + total_fill_rect_buffer_sets
        + total_fill_poly_buffer_sets
        + total_thick_line_buffer_sets
        + total_dashed_line_buffer_sets;
    const std::size_t shared_render_buffer_objects =
        (frame.mvp_ubuf ? 1u : 0u)
        + (frame.style_ubuf ? 1u : 0u)
        + (m_thick_line_corner_vbuf ? 1u : 0u)
        + (m_overlay_quad_vbuf ? 1u : 0u);
    const std::size_t total_render_buffer_objects =
        total_line_buffer_sets
        + total_fill_rect_buffer_sets
        + total_fill_poly_buffer_sets
        + total_thick_line_buffer_sets
        + total_dashed_line_buffer_sets
        + shared_render_buffer_objects;

    std::vector<bool> visible_line_buffer_mask(total_line_buffer_sets, false);
    std::vector<bool> visible_fill_rect_buffer_mask(total_fill_rect_buffer_sets, false);
    std::vector<bool> visible_fill_poly_buffer_mask(total_fill_poly_buffer_sets, false);
    std::vector<bool> visible_thick_line_buffer_mask(total_thick_line_buffer_sets, false);
    std::vector<bool> visible_dashed_line_buffer_mask(total_dashed_line_buffer_sets, false);
    auto countVisibleBuffers = [](const std::vector<bool>& mask) {
        return std::count(mask.begin(), mask.end(), true);
    };
    auto countTotalChunks = [](const auto& styles) {
        std::size_t total = 0;
        for (const auto& style : styles)
            total += style.chunks.size();
        return total;
    };

    std::size_t considered_chunk_count = 0;
    std::size_t visible_chunk_count = 0;
    std::size_t visible_line_chunks = 0;
    std::size_t visible_fill_rect_chunks = 0;
    std::size_t visible_fill_poly_chunks = 0;
    std::size_t visible_thick_line_chunks = 0;
    std::size_t visible_dashed_line_chunks = 0;
    unsigned long long visible_line_verts = 0;
    unsigned long long visible_fill_rects = 0;
    unsigned long long visible_fill_poly_verts = 0;
    unsigned long long visible_thick_line_instances = 0;
    unsigned long long visible_dashed_line_instances = 0;

    cb->setGraphicsPipeline(m_fill_rect_pso.get());
    for (const GpuStyleBuffer& style : frame.gpu_scene.fill_rects) {
        bool style_bound = false;
        const QRhiCommandBuffer::DynamicOffset dyn_offset{1, style.style_offset};
        for (const GpuChunk& chunk : style.chunks) {
            ++considered_chunk_count;
            if (!rectanglesIntersect(chunk.world_bounds, visible_world))
                continue;
            if (!style_bound) {
                cb->setShaderResources(frame.srb.get(), 1, &dyn_offset);
                style_bound = true;
            }
            ++visible_chunk_count;
            ++visible_fill_rect_chunks;
            visible_fill_rects += chunk.count;
            if (std::size_t(chunk.buffer_index) < visible_fill_rect_buffer_mask.size())
                visible_fill_rect_buffer_mask[std::size_t(chunk.buffer_index)] = true;
            const QRhiCommandBuffer::VertexInput input = {
                frame.fill_rect_instance_vbufs[chunk.buffer_index].get(), chunk.byte_offset
            };
            cb->setVertexInput(0, 1, &input);
            cb->draw(4, chunk.count);
        }
    }

    cb->setGraphicsPipeline(m_fill_poly_pso.get());
    for (const GpuStyleBuffer& style : frame.gpu_scene.fill_polys) {
        bool style_bound = false;
        const QRhiCommandBuffer::DynamicOffset dyn_offset{1, style.style_offset};
        for (const GpuChunk& chunk : style.chunks) {
            ++considered_chunk_count;
            if (!rectanglesIntersect(chunk.world_bounds, visible_world))
                continue;
            if (!style_bound) {
                cb->setShaderResources(frame.srb.get(), 1, &dyn_offset);
                style_bound = true;
            }
            ++visible_chunk_count;
            ++visible_fill_poly_chunks;
            visible_fill_poly_verts += chunk.count;
            if (std::size_t(chunk.buffer_index) < visible_fill_poly_buffer_mask.size())
                visible_fill_poly_buffer_mask[std::size_t(chunk.buffer_index)] = true;
            const QRhiCommandBuffer::VertexInput input = {
                frame.fill_poly_vbufs[chunk.buffer_index].get(), chunk.byte_offset
            };
            cb->setVertexInput(0, 1, &input);
            cb->draw(chunk.count);
        }
    }

    cb->setGraphicsPipeline(m_line_pso.get());
    for (const GpuStyleBuffer& style : frame.gpu_scene.thin_lines) {
        bool style_bound = false;
        const QRhiCommandBuffer::DynamicOffset dyn_offset{1, style.style_offset};
        for (const GpuChunk& chunk : style.chunks) {
            ++considered_chunk_count;
            if (!rectanglesIntersect(chunk.world_bounds, visible_world))
                continue;
            if (!style_bound) {
                cb->setShaderResources(frame.srb.get(), 1, &dyn_offset);
                style_bound = true;
            }
            ++visible_chunk_count;
            ++visible_line_chunks;
            visible_line_verts += chunk.count;
            if (std::size_t(chunk.buffer_index) < visible_line_buffer_mask.size())
                visible_line_buffer_mask[std::size_t(chunk.buffer_index)] = true;
            const QRhiCommandBuffer::VertexInput input = {
                frame.thin_line_vbufs[chunk.buffer_index].get(), chunk.byte_offset
            };
            cb->setVertexInput(0, 1, &input);
            cb->draw(chunk.count);
        }
    }

    cb->setGraphicsPipeline(m_dashed_line_pso.get());
    for (const GpuStyleBuffer& style : frame.gpu_scene.dashed_lines) {
        bool style_bound = false;
        const QRhiCommandBuffer::DynamicOffset dyn_offset{1, style.style_offset};
        for (const GpuChunk& chunk : style.chunks) {
            ++considered_chunk_count;
            if (!rectanglesIntersect(chunk.world_bounds, visible_world))
                continue;
            if (!style_bound) {
                cb->setShaderResources(frame.srb.get(), 1, &dyn_offset);
                style_bound = true;
            }
            ++visible_chunk_count;
            ++visible_dashed_line_chunks;
            visible_dashed_line_instances += chunk.count;
            if (std::size_t(chunk.buffer_index) < visible_dashed_line_buffer_mask.size())
                visible_dashed_line_buffer_mask[std::size_t(chunk.buffer_index)] = true;
            const QRhiCommandBuffer::VertexInput inputs[2] = {
                { m_thick_line_corner_vbuf.get(), 0 },
                { frame.dashed_line_instance_vbufs[chunk.buffer_index].get(), chunk.byte_offset }
            };
            cb->setVertexInput(0, 2, inputs);
            cb->draw(4, chunk.count);
        }
    }

    cb->setGraphicsPipeline(m_thick_line_pso.get());
    for (const GpuStyleBuffer& style : frame.gpu_scene.thick_lines) {
        bool style_bound = false;
        const QRhiCommandBuffer::DynamicOffset dyn_offset{1, style.style_offset};
        for (const GpuChunk& chunk : style.chunks) {
            ++considered_chunk_count;
            if (!rectanglesIntersect(chunk.world_bounds, visible_world))
                continue;
            if (!style_bound) {
                cb->setShaderResources(frame.srb.get(), 1, &dyn_offset);
                style_bound = true;
            }
            ++visible_chunk_count;
            ++visible_thick_line_chunks;
            visible_thick_line_instances += chunk.count;
            if (std::size_t(chunk.buffer_index) < visible_thick_line_buffer_mask.size())
                visible_thick_line_buffer_mask[std::size_t(chunk.buffer_index)] = true;
            const QRhiCommandBuffer::VertexInput inputs[2] = {
                { m_thick_line_corner_vbuf.get(), 0 },
                { frame.thick_line_instance_vbufs[chunk.buffer_index].get(), chunk.byte_offset }
            };
            cb->setVertexInput(0, 2, inputs);
            cb->draw(4, chunk.count);
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

#ifdef EZGL_RENDERER_DEBUG
    const double frame_ms = frame_timer.elapsed_ms();
    const std::size_t visible_line_buffer_sets        = countVisibleBuffers(visible_line_buffer_mask);
    const std::size_t visible_fill_rect_buffer_sets   = countVisibleBuffers(visible_fill_rect_buffer_mask);
    const std::size_t visible_fill_poly_buffer_sets   = countVisibleBuffers(visible_fill_poly_buffer_mask);
    const std::size_t visible_thick_line_buffer_sets  = countVisibleBuffers(visible_thick_line_buffer_mask);
    const std::size_t visible_dashed_line_buffer_sets = countVisibleBuffers(visible_dashed_line_buffer_mask);
    const std::size_t visible_stream_buffer_sets =
        visible_line_buffer_sets
        + visible_fill_rect_buffer_sets
        + visible_fill_poly_buffer_sets
        + visible_thick_line_buffer_sets
        + visible_dashed_line_buffer_sets;
    const std::size_t visible_render_buffer_objects =
        visible_line_buffer_sets
        + visible_fill_rect_buffer_sets
        + visible_fill_poly_buffer_sets
        + visible_thick_line_buffer_sets
        + visible_dashed_line_buffer_sets
        + shared_render_buffer_objects;

    q_debug_stream()
        << std::fixed << std::setprecision(3)
        << "RHI render() CPU time " << frame_ms << " ms"
        << " (frame_slot=" << frame_slot
        << ", geom_dirty=" << int(geom_dirty)
        << ", mvp_only=" << int(mvp_only_frame) << ")"
        << " styles("
            << "thin=" << frame.gpu_scene.thin_lines.size()
            << " fill_rect=" << frame.gpu_scene.fill_rects.size()
            << " fill_poly=" << frame.gpu_scene.fill_polys.size()
            << " thick=" << frame.gpu_scene.thick_lines.size()
            << " dashed=" << frame.gpu_scene.dashed_lines.size()
        << ")"
        << " chunks("
            << "total=" << considered_chunk_count
            << " visible=" << visible_chunk_count
            << " thin=" << countTotalChunks(frame.gpu_scene.thin_lines) << "/" << visible_line_chunks
            << " fill_rect=" << countTotalChunks(frame.gpu_scene.fill_rects) << "/" << visible_fill_rect_chunks
            << " fill_poly=" << countTotalChunks(frame.gpu_scene.fill_polys) << "/" << visible_fill_poly_chunks
            << " thick=" << countTotalChunks(frame.gpu_scene.thick_lines) << "/" << visible_thick_line_chunks
            << " dashed=" << countTotalChunks(frame.gpu_scene.dashed_lines) << "/" << visible_dashed_line_chunks
        << ")"
        << " prims("
            << "thin_verts=" << visible_line_verts
            << " fill_rects=" << visible_fill_rects
            << " fill_poly_verts=" << visible_fill_poly_verts
            << " thick_lines=" << visible_thick_line_instances
            << " dashed_lines=" << visible_dashed_line_instances
        << ")"
        << " buffer_sets("
            << "total=" << total_stream_buffer_sets
            << " visible=" << visible_stream_buffer_sets
            << " thin=" << total_line_buffer_sets << "/" << visible_line_buffer_sets
            << " fill_rect=" << total_fill_rect_buffer_sets << "/" << visible_fill_rect_buffer_sets
            << " fill_poly=" << total_fill_poly_buffer_sets << "/" << visible_fill_poly_buffer_sets
            << " thick=" << total_thick_line_buffer_sets << "/" << visible_thick_line_buffer_sets
            << " dashed=" << total_dashed_line_buffer_sets << "/" << visible_dashed_line_buffer_sets
        << ")"
        << " buffer_objects("
            << "total=" << total_render_buffer_objects
            << " visible=" << visible_render_buffer_objects
            << " shared=" << shared_render_buffer_objects
        << ")";
#endif // EZGL_RENDERER_DEBUG
}

void RhiCanvasWidget::releaseResources()
{
    auto resetBufferVector = [](std::vector<std::unique_ptr<QRhiBuffer>>& buffers) {
        buffers.clear();
    };

    m_overlay_pso.reset();
    m_dashed_line_pso.reset();
    m_thick_line_pso.reset();
    m_fill_poly_pso.reset();
    m_fill_rect_pso.reset();
    m_line_pso.reset();
    m_overlay_sampler.reset();
    m_overlay_quad_vbuf.reset();
    m_thick_line_corner_vbuf.reset();
    for (FrameResources& frame : m_frame_resources) {
        frame.overlay_srb.reset();
        frame.overlay_tex.reset();
        frame.srb.reset();
        resetBufferVector(frame.dashed_line_instance_vbufs);
        resetBufferVector(frame.thick_line_instance_vbufs);
        resetBufferVector(frame.fill_poly_vbufs);
        resetBufferVector(frame.fill_rect_instance_vbufs);
        resetBufferVector(frame.thin_line_vbufs);
        frame.gpu_scene.clear();
        frame.style_ubuf.reset();
        frame.mvp_ubuf.reset();
    }
    m_frame_resources.clear();
    m_frame_slot_geom_valid.clear();
    m_initialized = false;
}

void RhiCanvasWidget::resizeEvent(QResizeEvent* e)
{
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
