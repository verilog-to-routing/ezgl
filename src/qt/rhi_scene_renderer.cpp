#include "ezgl/qt/rhi_scene_renderer.hpp"
#include "ezgl/qt/render_backend.hpp"
#include "ezgl/logutils.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <memory>
#include <vector>

#include <rhi/qrhi.h>
#include <rhi/qshader.h>
#include <QFile>
#include <QImage>

// ---- anonymous-namespace helpers (formerly in rhi_canvas_widget.cpp) -------

namespace {

constexpr std::size_t kMaxQrhiBufferBytes =
    std::size_t(std::numeric_limits<int>::max());
constexpr std::size_t kInitialThinLineBufferBytes       = 1 * 1024 * 1024;
constexpr std::size_t kInitialFillRectBufferBytes       = 512 * 1024;
constexpr std::size_t kInitialFillPolyBufferBytes       = 512 * 1024;
constexpr std::size_t kInitialThickInstanceBufferBytes  = 512 * 1024;
constexpr std::size_t kInitialDashedInstanceBufferBytes = 512 * 1024;
constexpr std::size_t kInitialArrowInstanceBufferBytes  = 512 * 1024;
constexpr std::size_t kInitialStyleUniformBufferBytes   = 16 * 1024;

constexpr std::size_t kMaxPosVerticesPerBuffer =
    kMaxQrhiBufferBytes / sizeof(ezgl::PosVertex);
constexpr std::size_t kMaxFillRectInstancesPerBuffer =
    kMaxQrhiBufferBytes / sizeof(ezgl::FillRectInstance);
constexpr std::size_t kMaxThickInstancesPerBuffer =
    kMaxQrhiBufferBytes / sizeof(ezgl::ThickLineInstance);
constexpr std::size_t kMaxDashedInstancesPerBuffer =
    kMaxQrhiBufferBytes / sizeof(ezgl::DashedLineInstance);
constexpr std::size_t kMaxArrowInstancesPerBuffer =
    kMaxQrhiBufferBytes / sizeof(ezgl::ArrowInstance);

// MVP UBO layout (std140, binding 0):
//   offset  0 : mat4  mvp      (64 bytes)
//   offset 64 : vec2  viewport (8 bytes — widget size in pixels)
//   padding   : 8 bytes  →  total 80 bytes (16-byte aligned)
static constexpr int kMvpUboSize = 80;

struct StyleUniform {
    float color[4];
    float line[4]; // x: width_px, y: dash_px, z: gap_px, w: unused
};
static_assert(sizeof(StyleUniform) == 32, "StyleUniform must match two std140 vec4 values");

struct OverlayVertex { float x, y, u, v; };
static_assert(sizeof(OverlayVertex) == 16, "OverlayVertex must be 16 bytes");

std::size_t alignUp(std::size_t value, std::size_t alignment)
{
    if (alignment == 0) return value;
    const std::size_t rem = value % alignment;
    return rem == 0 ? value : (value + alignment - rem);
}

StyleUniform makeStyleUniform(ezgl::StyleKey style_key, std::uint32_t rgba)
{
    constexpr float kScale = 1.0f / 255.0f;
    float width_px = float(ezgl::style_key_line_width(style_key));
    if (width_px <= 0.0f) width_px = 1.0f;
    float dash_px = 0.0f, gap_px = 0.0f;
    if (ezgl::style_key_line_dash(style_key) != 0) {
        dash_px = 5.0f;
        gap_px  = 3.0f;
    }
    return StyleUniform{{
        float((rgba >>  0) & 0xFF) * kScale,
        float((rgba >>  8) & 0xFF) * kScale,
        float((rgba >> 16) & 0xFF) * kScale,
        float((rgba >> 24) & 0xFF) * kScale
    }, {width_px, dash_px, gap_px, 0.0f}};
}

QShader loadShader(const char* resource_path)
{
    QFile f(resource_path);
    if (!f.open(QIODevice::ReadOnly))
        qFatal("RhiSceneRenderer: cannot open shader resource %s", resource_path);
    return QShader::fromSerialized(f.readAll());
}

bool ensureDynamicBuf(QRhi*                        rhi,
                      std::unique_ptr<QRhiBuffer>& buf,
                      QRhiBuffer::UsageFlags       usage,
                      std::size_t                  needed_bytes,
                      std::size_t                  initial_bytes)
{
    if (buf && std::size_t(buf->size()) >= needed_bytes)
        return false;
    if (needed_bytes > kMaxQrhiBufferBytes)
        qFatal("RhiSceneRenderer: GPU buffer size %zu exceeds int limit", needed_bytes);

    std::size_t new_size = buf ? std::size_t(buf->size()) : initial_bytes;
    while (new_size < needed_bytes) {
        if (new_size > kMaxQrhiBufferBytes / 2) { new_size = kMaxQrhiBufferBytes; break; }
        new_size *= 2;
    }

    QRhiBuffer* old = buf.release();
    if (old) old->deleteLater();
    buf.reset(rhi->newBuffer(QRhiBuffer::Dynamic, usage, int(new_size)));
    buf->create();
    return true;
}

void releaseDynamicBuf(std::unique_ptr<QRhiBuffer>& buf)
{
    QRhiBuffer* old = buf.release();
    if (old) old->deleteLater();
}

void buildPipeline(QRhi*                                   rhi,
                   std::unique_ptr<QRhiGraphicsPipeline>&  pso,
                   QRhiGraphicsPipeline::Topology           topology,
                   const QShader&                           vs,
                   const QShader&                           fs,
                   QRhiShaderResourceBindings*              srb,
                   QRhiRenderPassDescriptor*                rpDesc)
{
    QRhiVertexInputLayout layout;
    layout.setBindings({ QRhiVertexInputBinding(sizeof(ezgl::PosVertex)) });
    layout.setAttributes({ QRhiVertexInputAttribute(
        0, 0, QRhiVertexInputAttribute::Float2, offsetof(ezgl::PosVertex, x)) });

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable   = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    pso.reset(rhi->newGraphicsPipeline());
    pso->setTopology(topology);
    pso->setVertexInputLayout(layout);
    pso->setShaderStages({{ QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs }});
    pso->setShaderResourceBindings(srb);
    pso->setRenderPassDescriptor(rpDesc);
    pso->setTargetBlends({ blend });
    pso->setDepthTest(false);
    pso->setDepthWrite(false);
    pso->setSampleCount(ezgl::EZGL_RHI_SAMPLE_COUNT);
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
    pso->setShaderStages({{ QRhiShaderStage::Vertex, vs }, { QRhiShaderStage::Fragment, fs }});
    pso->setShaderResourceBindings(srb);
    pso->setRenderPassDescriptor(rpDesc);
    pso->setTargetBlends({ blend });
    pso->setDepthTest(false);
    pso->setDepthWrite(false);
    pso->setSampleCount(ezgl::EZGL_RHI_SAMPLE_COUNT);
    pso->create();
}

void buildThickLinePipeline(QRhi*                                  rhi,
                            std::unique_ptr<QRhiGraphicsPipeline>& pso,
                            const QShader&                         thick_vs,
                            const QShader&                         fs,
                            QRhiShaderResourceBindings*            srb,
                            QRhiRenderPassDescriptor*              rpDesc)
{
    QRhiVertexInputLayout layout;
    layout.setBindings({
        QRhiVertexInputBinding(sizeof(ezgl::QuadCorner)),
        QRhiVertexInputBinding(sizeof(ezgl::ThickLineInstance),
                               QRhiVertexInputBinding::PerInstance)
    });
    layout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::QuadCorner, t)),
        QRhiVertexInputAttribute(1, 1, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::ThickLineInstance, x0)),
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
    pso->setShaderStages({{ QRhiShaderStage::Vertex, thick_vs }, { QRhiShaderStage::Fragment, fs }});
    pso->setShaderResourceBindings(srb);
    pso->setRenderPassDescriptor(rpDesc);
    pso->setTargetBlends({ blend });
    pso->setDepthTest(false);
    pso->setDepthWrite(false);
    pso->setSampleCount(ezgl::EZGL_RHI_SAMPLE_COUNT);
    pso->create();
}

// Pipeline for GPU-instanced arrow heads. Each instance is a 4-float
// vec4 {anchor, dir} read at attribute location 0/1; the vertex shader
// runs 3 times per instance and uses gl_VertexIndex (no per-vertex
// buffer needed). Fragment stage reuses the line.frag solid-colour shader.
void buildArrowPipeline(QRhi*                                  rhi,
                        std::unique_ptr<QRhiGraphicsPipeline>& pso,
                        const QShader&                         arrow_vs,
                        const QShader&                         fs,
                        QRhiShaderResourceBindings*            srb,
                        QRhiRenderPassDescriptor*              rpDesc)
{
    QRhiVertexInputLayout layout;
    layout.setBindings({
        QRhiVertexInputBinding(sizeof(ezgl::ArrowInstance),
                               QRhiVertexInputBinding::PerInstance)
    });
    layout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::ArrowInstance, ax)),
        QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float2,
                                 offsetof(ezgl::ArrowInstance, dx))
    });

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable   = true;
    blend.srcColor = QRhiGraphicsPipeline::SrcAlpha;
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    pso.reset(rhi->newGraphicsPipeline());
    pso->setTopology(QRhiGraphicsPipeline::Triangles);
    pso->setVertexInputLayout(layout);
    pso->setShaderStages({{ QRhiShaderStage::Vertex, arrow_vs }, { QRhiShaderStage::Fragment, fs }});
    pso->setShaderResourceBindings(srb);
    pso->setRenderPassDescriptor(rpDesc);
    pso->setTargetBlends({ blend });
    pso->setDepthTest(false);
    pso->setDepthWrite(false);
    pso->setSampleCount(ezgl::EZGL_RHI_SAMPLE_COUNT);
    pso->create();
}

void buildDashedLinePipeline(QRhi*                                  rhi,
                             std::unique_ptr<QRhiGraphicsPipeline>& pso,
                             const QShader&                         dashed_vs,
                             const QShader&                         dashed_fs,
                             QRhiShaderResourceBindings*            srb,
                             QRhiRenderPassDescriptor*              rpDesc)
{
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
    pso->setShaderStages({{ QRhiShaderStage::Vertex, dashed_vs }, { QRhiShaderStage::Fragment, dashed_fs }});
    pso->setShaderResourceBindings(srb);
    pso->setRenderPassDescriptor(rpDesc);
    pso->setTargetBlends({ blend });
    pso->setDepthTest(false);
    pso->setDepthWrite(false);
    pso->setSampleCount(ezgl::EZGL_RHI_SAMPLE_COUNT);
    pso->create();
}

void buildOverlayPipeline(QRhi*                                  rhi,
                          std::unique_ptr<QRhiGraphicsPipeline>& pso,
                          const QShader&                         overlay_vs,
                          const QShader&                         overlay_fs,
                          QRhiShaderResourceBindings*            srb,
                          QRhiRenderPassDescriptor*              rpDesc)
{
    QRhiVertexInputLayout layout;
    layout.setBindings({ QRhiVertexInputBinding(sizeof(OverlayVertex)) });
    layout.setAttributes({
        QRhiVertexInputAttribute(0, 0, QRhiVertexInputAttribute::Float2,
                                 offsetof(OverlayVertex, x)),
        QRhiVertexInputAttribute(0, 1, QRhiVertexInputAttribute::Float2,
                                 offsetof(OverlayVertex, u))
    });

    QRhiGraphicsPipeline::TargetBlend blend;
    blend.enable   = true;
    blend.srcColor = QRhiGraphicsPipeline::One;             // premultiplied overlay
    blend.dstColor = QRhiGraphicsPipeline::OneMinusSrcAlpha;
    blend.srcAlpha = QRhiGraphicsPipeline::One;
    blend.dstAlpha = QRhiGraphicsPipeline::OneMinusSrcAlpha;

    pso.reset(rhi->newGraphicsPipeline());
    pso->setTopology(QRhiGraphicsPipeline::TriangleStrip);
    pso->setVertexInputLayout(layout);
    pso->setShaderStages({{ QRhiShaderStage::Vertex, overlay_vs }, { QRhiShaderStage::Fragment, overlay_fs }});
    pso->setShaderResourceBindings(srb);
    pso->setRenderPassDescriptor(rpDesc);
    pso->setTargetBlends({ blend });
    pso->setDepthTest(false);
    pso->setDepthWrite(false);
    pso->setSampleCount(ezgl::EZGL_RHI_SAMPLE_COUNT);
    pso->create();
}

bool rectanglesIntersect(const ezgl::rectangle& a, const ezgl::rectangle& b)
{
    return !(a.right() < b.left() || a.left() > b.right()
          || a.top()   < b.bottom() || a.bottom() > b.top());
}

} // anonymous namespace

// ============================================================================

namespace ezgl {

// ---- RhiSceneRenderer ------------------------------------------------------

RhiSceneRenderer::~RhiSceneRenderer()
{
    release();
}

void RhiSceneRenderer::initialize(QRhi* rhi, QRhiRenderPassDescriptor* rp_desc)
{
    release(); // safe to re-initialize

    m_rhi = rhi;

    QShader line_vs      = loadShader(":/ezgl/line.vert.qsb");
    QShader line_fs      = loadShader(":/ezgl/line.frag.qsb");
    QShader fill_rect_vs = loadShader(":/ezgl/fill_rect.vert.qsb");
    QShader thick_vs     = loadShader(":/ezgl/thick_line.vert.qsb");
    QShader dashed_vs    = loadShader(":/ezgl/dashed_line.vert.qsb");
    QShader arrow_vs     = loadShader(":/ezgl/arrow.vert.qsb");
    QShader dashed_fs    = loadShader(":/ezgl/dashed_line.frag.qsb");
    QShader overlay_vs   = loadShader(":/ezgl/overlay.vert.qsb");
    QShader overlay_fs   = loadShader(":/ezgl/overlay.frag.qsb");

    const int n_slots = std::max(1, rhi->resourceLimit(QRhi::FramesInFlight));
    m_frame_resources.clear();
    m_frame_resources.resize(std::size_t(n_slots));
    m_frame_slot_geom_valid.assign(std::size_t(n_slots), false);

    // Linear, not Nearest: the overlay QImage is the same size as the
    // framebuffer in the common case, but HiDPI / DPR != 1 / fractional
    // scaling all introduce non-integer scaling at blit time. Nearest
    // there produces blocky text edges; Linear preserves the QPainter's
    // antialiased glyphs cleanly.
    m_overlay_sampler.reset(rhi->newSampler(
        QRhiSampler::Linear, QRhiSampler::Linear, QRhiSampler::None,
        QRhiSampler::ClampToEdge, QRhiSampler::ClampToEdge));
    m_overlay_sampler->create();

    for (FrameResources& fr : m_frame_resources) {
        fr.mvp_ubuf.reset(rhi->newBuffer(QRhiBuffer::Dynamic,
                                         QRhiBuffer::UniformBuffer, kMvpUboSize));
        fr.mvp_ubuf->create();
        fr.style_ubuf.reset(rhi->newBuffer(
            QRhiBuffer::Dynamic, QRhiBuffer::UniformBuffer,
            int(std::max<std::size_t>(kInitialStyleUniformBufferBytes,
                                      std::size_t(rhi->ubufAlignment())))));
        fr.style_ubuf->create();
        fr.overlay_tex.reset(rhi->newTexture(QRhiTexture::RGBA8, QSize(1, 1)));
        fr.overlay_tex->create();
        fr.thin_line_vbufs.clear();
        fr.fill_rect_instance_vbufs.clear();
        fr.fill_poly_vbufs.clear();
        fr.thick_line_instance_vbufs.clear();
        fr.dashed_line_instance_vbufs.clear();
        fr.arrow_instance_vbufs.clear();
        fr.gpu_scene.clear();
    }

    m_thick_line_corner_vbuf.reset(rhi->newBuffer(
        QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
        int(4 * sizeof(QuadCorner))));
    m_thick_line_corner_vbuf->create();

    m_overlay_quad_vbuf.reset(rhi->newBuffer(
        QRhiBuffer::Dynamic, QRhiBuffer::VertexBuffer,
        int(4 * sizeof(OverlayVertex))));
    m_overlay_quad_vbuf->create();

    for (FrameResources& fr : m_frame_resources) {
        fr.srb.reset(rhi->newShaderResourceBindings());
        fr.srb->setBindings({
            QRhiShaderResourceBinding::uniformBuffer(
                0, QRhiShaderResourceBinding::VertexStage, fr.mvp_ubuf.get()),
            QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                1,
                QRhiShaderResourceBinding::VertexStage |
                QRhiShaderResourceBinding::FragmentStage,
                fr.style_ubuf.get(), sizeof(StyleUniform))
        });
        fr.srb->create();

        fr.overlay_srb.reset(rhi->newShaderResourceBindings());
        fr.overlay_srb->setBindings({
            QRhiShaderResourceBinding::sampledTexture(
                0, QRhiShaderResourceBinding::FragmentStage,
                fr.overlay_tex.get(), m_overlay_sampler.get())
        });
        fr.overlay_srb->create();
    }

    // Build pipelines — use the first frame's SRBs as the template.
    // Qt RHI clones the layout so per-frame SRBs can be swapped in at draw time.
    auto* geom_srb    = m_frame_resources.front().srb.get();
    auto* over_srb    = m_frame_resources.front().overlay_srb.get();
    buildPipeline(rhi, m_line_pso, QRhiGraphicsPipeline::Lines,
                  line_vs, line_fs, geom_srb, rp_desc);
    buildFillRectPipeline(rhi, m_fill_rect_pso,
                          fill_rect_vs, line_fs, geom_srb, rp_desc);
    buildPipeline(rhi, m_fill_poly_pso, QRhiGraphicsPipeline::Triangles,
                  line_vs, line_fs, geom_srb, rp_desc);
    buildThickLinePipeline(rhi, m_thick_line_pso,
                           thick_vs, line_fs, geom_srb, rp_desc);
    buildDashedLinePipeline(rhi, m_dashed_line_pso,
                            dashed_vs, dashed_fs, geom_srb, rp_desc);
    buildArrowPipeline(rhi, m_arrow_pso,
                       arrow_vs, line_fs, geom_srb, rp_desc);
    buildOverlayPipeline(rhi, m_overlay_pso,
                         overlay_vs, overlay_fs, over_srb, rp_desc);

    m_initialized = true;
}

void RhiSceneRenderer::render(QRhiCommandBuffer*                         cb,
                               QRhiRenderTarget*                          rt,
                               const QSize&                               pixel_size,
                               int                                        frame_slot,
                               bool                                       geom_dirty,
                               const std::shared_ptr<const SceneBuffers>& scene,
                               const QMatrix4x4&                          mvp,
                               const rectangle&                           visible_world,
                               const QImage&                              overlay_in,
                               QColor                                     bg)
{
    if (!m_initialized || m_frame_resources.empty())
        return;

    // Update the per-frame-slot geometry cache if needed.
    if (geom_dirty && scene) {
        m_cached_scene = scene;
        std::fill(m_frame_slot_geom_valid.begin(), m_frame_slot_geom_valid.end(), false);
    }

    const bool need_geom_for_slot =
        std::size_t(frame_slot) < m_frame_slot_geom_valid.size()
        && !m_frame_slot_geom_valid[std::size_t(frame_slot)]
        && m_cached_scene;

    const bool upload_geom = geom_dirty || need_geom_for_slot;
    const std::shared_ptr<const SceneBuffers>& scene_buffers =
        upload_geom ? (scene ? scene : m_cached_scene) : nullptr;

    FrameResources& fr = m_frame_resources[std::size_t(frame_slot)];

    QImage overlay = overlay_in;
    const bool has_overlay = !overlay.isNull();
    if (has_overlay)
        overlay = overlay.convertToFormat(QImage::Format_RGBA8888_Premultiplied);

    // ---- Upload -------------------------------------------------------------
    QRhiResourceUpdateBatch* u = m_rhi->nextResourceUpdateBatch();

    // MVP + viewport (device pixels so thick-line width is correct on HiDPI)
    u->updateDynamicBuffer(fr.mvp_ubuf.get(), 0, 64, mvp.constData());
    {
        const float vp[2] = { float(pixel_size.width()), float(pixel_size.height()) };
        u->updateDynamicBuffer(fr.mvp_ubuf.get(), 64, int(sizeof(vp)), vp);
    }

    // Constant quad-corner buffer (same every frame, safe after re-init)
    {
        static const QuadCorner kCorners[4] = {
            { 0.0f, -1.0f }, { 0.0f, +1.0f }, { 1.0f, -1.0f }, { 1.0f, +1.0f }
        };
        u->updateDynamicBuffer(m_thick_line_corner_vbuf.get(), 0,
                               int(sizeof(kCorners)), kCorners);
    }
    {
        static const OverlayVertex kQuad[4] = {
            { -1.0f, +1.0f, 0.0f, 0.0f }, { -1.0f, -1.0f, 0.0f, 1.0f },
            { +1.0f, +1.0f, 1.0f, 0.0f }, { +1.0f, -1.0f, 1.0f, 1.0f }
        };
        u->updateDynamicBuffer(m_overlay_quad_vbuf.get(), 0,
                               int(sizeof(kQuad)), kQuad);
    }

    // Overlay texture
    if (has_overlay) {
        const QSize overlay_size = overlay.size();
        if (!fr.overlay_tex || fr.overlay_tex->pixelSize() != overlay_size) {
            fr.overlay_srb.reset();
            fr.overlay_tex.reset(m_rhi->newTexture(QRhiTexture::RGBA8, overlay_size));
            fr.overlay_tex->create();
            fr.overlay_srb.reset(m_rhi->newShaderResourceBindings());
            fr.overlay_srb->setBindings({
                QRhiShaderResourceBinding::sampledTexture(
                    0, QRhiShaderResourceBinding::FragmentStage,
                    fr.overlay_tex.get(), m_overlay_sampler.get())
            });
            fr.overlay_srb->create();
        }
        u->uploadTexture(fr.overlay_tex.get(), overlay);
    }

    // Geometry: style UBO + vertex buffers
    if (upload_geom && scene_buffers) {
        struct PendingUpload {
            quint32     buffer_index;
            quint32     byte_offset;
            quint32     byte_size;
            const void* data;
        };

        const std::size_t style_stride =
            alignUp(sizeof(StyleUniform), std::size_t(m_rhi->ubufAlignment()));
        const std::size_t total_style_count =
            scene_buffers->thin_lines.size() + scene_buffers->fill_rects.size()
            + scene_buffers->fill_polys.size() + scene_buffers->thick_lines.size()
            + scene_buffers->dashed_lines.size() + scene_buffers->arrows.size();

        std::vector<std::uint8_t> style_uniform_bytes(total_style_count * style_stride, 0);
        std::size_t next_style_index = 0;
        auto assign_style_offset = [&](StyleKey sk, std::uint32_t rgba) {
            const std::size_t offset = next_style_index * style_stride;
            const StyleUniform su = makeStyleUniform(sk, rgba);
            std::memcpy(style_uniform_bytes.data() + offset, &su, sizeof(StyleUniform));
            ++next_style_index;
            return quint32(offset);
        };

        std::vector<std::size_t> thin_counts, fill_rect_counts, fill_poly_counts,
                                  thick_counts, dashed_counts, arrow_counts;
        std::vector<PendingUpload> thin_uploads, fill_rect_uploads, fill_poly_uploads,
                                    thick_uploads, dashed_uploads, arrow_uploads;
        fr.gpu_scene.clear();

        auto planStyleBuffers = [&](const auto& scene_map,
                                     auto&        gpu_buffers,
                                     auto&        uploads,
                                     auto&        buffer_counts,
                                     std::size_t  elem_size,
                                     std::size_t  max_per_buffer,
                                     auto         get_data) {
            for (const auto& [sk, sb] : scene_map) {
                const auto& data = get_data(sb);
                if (data.empty()) continue;

                GpuStyleBuffer gpu_buf;
                gpu_buf.style_key    = sk;
                gpu_buf.rgba         = sb.rgba;
                gpu_buf.style_offset = assign_style_offset(sk, sb.rgba);

                for (const Chunk& chunk : sb.chunks) {
                    std::size_t remaining  = chunk.count;
                    std::size_t data_offset = chunk.offset;
                    while (remaining > 0) {
                        if (buffer_counts.empty() || buffer_counts.back() == max_per_buffer)
                            buffer_counts.push_back(0);

                        const std::size_t buf_idx    = buffer_counts.size() - 1;
                        const std::size_t buf_offset = buffer_counts.back();
                        const std::size_t count      = std::min(remaining, max_per_buffer - buf_offset);
                        const std::size_t byte_off   = buf_offset * elem_size;
                        const std::size_t byte_sz    = count * elem_size;

                        gpu_buf.chunks.emplace_back(
                            GpuChunk{chunk.world_bounds, quint32(buf_idx),
                                     quint32(byte_off), quint32(count)});
                        uploads.emplace_back(
                            PendingUpload{quint32(buf_idx), quint32(byte_off),
                                          quint32(byte_sz),
                                          static_cast<const void*>(data.data() + data_offset)});
                        buffer_counts.back() += count;
                        remaining   -= count;
                        data_offset += count;
                    }
                }
                gpu_buffers.push_back(std::move(gpu_buf));
            }
        };

        planStyleBuffers(scene_buffers->thin_lines,   fr.gpu_scene.thin_lines,
                         thin_uploads,   thin_counts,  sizeof(PosVertex),
                         kMaxPosVerticesPerBuffer,
                         [](const ThinLineStyleBuffer& b) -> const auto& { return b.verts; });
        planStyleBuffers(scene_buffers->fill_rects,   fr.gpu_scene.fill_rects,
                         fill_rect_uploads, fill_rect_counts, sizeof(FillRectInstance),
                         kMaxFillRectInstancesPerBuffer,
                         [](const FillRectStyleBuffer& b) -> const auto& { return b.instances; });
        planStyleBuffers(scene_buffers->fill_polys,   fr.gpu_scene.fill_polys,
                         fill_poly_uploads, fill_poly_counts, sizeof(PosVertex),
                         kMaxPosVerticesPerBuffer,
                         [](const FillPolyStyleBuffer& b) -> const auto& { return b.verts; });
        planStyleBuffers(scene_buffers->thick_lines,  fr.gpu_scene.thick_lines,
                         thick_uploads, thick_counts, sizeof(ThickLineInstance),
                         kMaxThickInstancesPerBuffer,
                         [](const ThickLineStyleBuffer& b) -> const auto& { return b.instances; });
        planStyleBuffers(scene_buffers->dashed_lines, fr.gpu_scene.dashed_lines,
                         dashed_uploads, dashed_counts, sizeof(DashedLineInstance),
                         kMaxDashedInstancesPerBuffer,
                         [](const DashedLineStyleBuffer& b) -> const auto& { return b.instances; });
        planStyleBuffers(scene_buffers->arrows,       fr.gpu_scene.arrows,
                         arrow_uploads, arrow_counts, sizeof(ArrowInstance),
                         kMaxArrowInstancesPerBuffer,
                         [](const ArrowStyleBuffer& b) -> const auto& { return b.instances; });

        // Ensure / grow style UBO. If the buffer is reallocated, the
        // fr.srb that was built once in initialize() still references the
        // old (deleted-pending) QRhiBuffer pointer. D3D11/Metal/Vulkan
        // cache the SRB's resource references at create() time, so the
        // shader ends up reading stale data — symptom: blank scene with
        // only the screen-space overlay visible. OpenGL re-resolves the
        // SRB each draw and accidentally hides the bug on Linux. Rebuild
        // the SRB whenever style_ubuf is recreated so the new buffer
        // pointer is picked up. mvp_ubuf is never reallocated, so the
        // other binding is stable.
        const std::size_t style_ubuf_bytes =
            std::max(style_stride, style_uniform_bytes.empty() ? 0 : style_uniform_bytes.size());
        const bool style_ubuf_recreated = ensureDynamicBuf(
            m_rhi, fr.style_ubuf, QRhiBuffer::UniformBuffer,
            style_ubuf_bytes,
            std::max<std::size_t>(kInitialStyleUniformBufferBytes, style_stride));
        if (style_ubuf_recreated) {
            fr.srb->setBindings({
                QRhiShaderResourceBinding::uniformBuffer(
                    0, QRhiShaderResourceBinding::VertexStage, fr.mvp_ubuf.get()),
                QRhiShaderResourceBinding::uniformBufferWithDynamicOffset(
                    1,
                    QRhiShaderResourceBinding::VertexStage |
                    QRhiShaderResourceBinding::FragmentStage,
                    fr.style_ubuf.get(), sizeof(StyleUniform))
            });
            fr.srb->create();
        }
        if (!style_uniform_bytes.empty())
            u->updateDynamicBuffer(fr.style_ubuf.get(), 0,
                                   int(style_uniform_bytes.size()),
                                   style_uniform_bytes.data());

        // Ensure / grow vertex / instance buffers
        auto trimBuffers = [](std::vector<std::unique_ptr<QRhiBuffer>>& v, std::size_t keep) {
            for (std::size_t i = keep; i < v.size(); ++i) releaseDynamicBuf(v[i]);
            v.resize(keep);
        };
        auto ensurePool = [&](std::vector<std::unique_ptr<QRhiBuffer>>& pool,
                               const std::vector<std::size_t>&           counts,
                               std::size_t                               elem_size,
                               std::size_t                               initial_bytes) {
            if (pool.size() < counts.size()) pool.resize(counts.size());
            for (std::size_t i = 0; i < counts.size(); ++i) {
                if (counts[i] == 0) continue;
                ensureDynamicBuf(m_rhi, pool[i], QRhiBuffer::VertexBuffer,
                                 counts[i] * elem_size, initial_bytes);
            }
            trimBuffers(pool, counts.size());
        };
        ensurePool(fr.thin_line_vbufs,          thin_counts,      sizeof(PosVertex),        kInitialThinLineBufferBytes);
        ensurePool(fr.fill_rect_instance_vbufs, fill_rect_counts, sizeof(FillRectInstance), kInitialFillRectBufferBytes);
        ensurePool(fr.fill_poly_vbufs,          fill_poly_counts, sizeof(PosVertex),        kInitialFillPolyBufferBytes);
        ensurePool(fr.thick_line_instance_vbufs,thick_counts,     sizeof(ThickLineInstance),kInitialThickInstanceBufferBytes);
        ensurePool(fr.dashed_line_instance_vbufs,dashed_counts,   sizeof(DashedLineInstance),kInitialDashedInstanceBufferBytes);
        ensurePool(fr.arrow_instance_vbufs,    arrow_counts,     sizeof(ArrowInstance),    kInitialArrowInstanceBufferBytes);

        auto uploadPool = [&](std::vector<std::unique_ptr<QRhiBuffer>>& pool,
                               const std::vector<PendingUpload>&         uploads_list) {
            for (const PendingUpload& up : uploads_list)
                u->updateDynamicBuffer(pool[up.buffer_index].get(),
                                       int(up.byte_offset), int(up.byte_size), up.data);
        };
        uploadPool(fr.thin_line_vbufs,          thin_uploads);
        uploadPool(fr.fill_rect_instance_vbufs, fill_rect_uploads);
        uploadPool(fr.fill_poly_vbufs,          fill_poly_uploads);
        uploadPool(fr.thick_line_instance_vbufs,thick_uploads);
        uploadPool(fr.dashed_line_instance_vbufs,dashed_uploads);
        uploadPool(fr.arrow_instance_vbufs,    arrow_uploads);

        if (std::size_t(frame_slot) < m_frame_slot_geom_valid.size())
            m_frame_slot_geom_valid[std::size_t(frame_slot)] = true;
    }

    // ---- Record draw commands -----------------------------------------------
    cb->beginPass(rt, bg, { 1.0f, 0 }, u);
    cb->setViewport(QRhiViewport(0, 0, float(pixel_size.width()), float(pixel_size.height())));

    auto drawStyled = [&](QRhiGraphicsPipeline*            pso,
                           const std::vector<GpuStyleBuffer>& styles,
                           std::vector<std::unique_ptr<QRhiBuffer>>& vbufs,
                           bool instanced, bool use_corner_buf) {
        cb->setGraphicsPipeline(pso);
        for (const GpuStyleBuffer& style : styles) {
            const QRhiCommandBuffer::DynamicOffset dyn{1, style.style_offset};
            bool style_bound = false;
            for (const GpuChunk& chunk : style.chunks) {
                if (!rectanglesIntersect(chunk.world_bounds, visible_world)) continue;
                if (!style_bound) {
                    cb->setShaderResources(fr.srb.get(), 1, &dyn);
                    style_bound = true;
                }
                if (use_corner_buf) {
                    const QRhiCommandBuffer::VertexInput inputs[2] = {
                        { m_thick_line_corner_vbuf.get(), 0 },
                        { vbufs[chunk.buffer_index].get(), chunk.byte_offset }
                    };
                    cb->setVertexInput(0, 2, inputs);
                    cb->draw(4, chunk.count);
                } else if (instanced) {
                    const QRhiCommandBuffer::VertexInput vi{
                        vbufs[chunk.buffer_index].get(), chunk.byte_offset};
                    cb->setVertexInput(0, 1, &vi);
                    cb->draw(4, chunk.count); // TriangleStrip instanced
                } else {
                    const QRhiCommandBuffer::VertexInput vi{
                        vbufs[chunk.buffer_index].get(), chunk.byte_offset};
                    cb->setVertexInput(0, 1, &vi);
                    cb->draw(chunk.count);
                }
            }
        }
    };

    drawStyled(m_fill_rect_pso.get(),  fr.gpu_scene.fill_rects,  fr.fill_rect_instance_vbufs,  true,  false);
    drawStyled(m_fill_poly_pso.get(),  fr.gpu_scene.fill_polys,  fr.fill_poly_vbufs,            false, false);
    drawStyled(m_line_pso.get(),       fr.gpu_scene.thin_lines,  fr.thin_line_vbufs,            false, false);
    drawStyled(m_dashed_line_pso.get(),fr.gpu_scene.dashed_lines,fr.dashed_line_instance_vbufs, false, true);
    drawStyled(m_thick_line_pso.get(), fr.gpu_scene.thick_lines, fr.thick_line_instance_vbufs,  false, true);

    // Arrow heads — single per-instance vertex binding, 3 vertices per
    // instance (Triangles topology). The vertex shader picks the corner via
    // gl_VertexIndex and synthesises the arrow at a constant SCREEN-pixel
    // size from style.line.x = arrow_size_px.
    if (!fr.gpu_scene.arrows.empty()) {
        cb->setGraphicsPipeline(m_arrow_pso.get());
        for (const GpuStyleBuffer& style : fr.gpu_scene.arrows) {
            const QRhiCommandBuffer::DynamicOffset dyn{1, style.style_offset};
            bool style_bound = false;
            for (const GpuChunk& chunk : style.chunks) {
                if (!style_bound) {
                    cb->setShaderResources(fr.srb.get(), 1, &dyn);
                    style_bound = true;
                }
                const QRhiCommandBuffer::VertexInput vi{
                    fr.arrow_instance_vbufs[chunk.buffer_index].get(),
                    chunk.byte_offset};
                cb->setVertexInput(0, 1, &vi);
                cb->draw(3, chunk.count);
            }
        }
    }

    if (has_overlay && fr.overlay_tex) {
        cb->setGraphicsPipeline(m_overlay_pso.get());
        cb->setShaderResources(fr.overlay_srb.get());
        const QRhiCommandBuffer::VertexInput vi{m_overlay_quad_vbuf.get(), 0};
        cb->setVertexInput(0, 1, &vi);
        cb->draw(4);
    }

    cb->endPass();
}

void RhiSceneRenderer::release()
{
    m_overlay_pso.reset();
    m_arrow_pso.reset();
    m_dashed_line_pso.reset();
    m_thick_line_pso.reset();
    m_fill_poly_pso.reset();
    m_fill_rect_pso.reset();
    m_line_pso.reset();
    m_overlay_sampler.reset();
    m_overlay_quad_vbuf.reset();
    m_thick_line_corner_vbuf.reset();

    for (FrameResources& fr : m_frame_resources) {
        fr.overlay_srb.reset();
        fr.overlay_tex.reset();
        fr.srb.reset();
        fr.arrow_instance_vbufs.clear();
        fr.dashed_line_instance_vbufs.clear();
        fr.thick_line_instance_vbufs.clear();
        fr.fill_poly_vbufs.clear();
        fr.fill_rect_instance_vbufs.clear();
        fr.thin_line_vbufs.clear();
        fr.gpu_scene.clear();
        fr.style_ubuf.reset();
        fr.mvp_ubuf.reset();
    }
    m_frame_resources.clear();
    m_frame_slot_geom_valid.clear();
    m_initialized = false;
    m_rhi = nullptr;
}

void RhiSceneRenderer::invalidate_geometry_cache()
{
    std::fill(m_frame_slot_geom_valid.begin(), m_frame_slot_geom_valid.end(), false);
}

} // namespace ezgl
