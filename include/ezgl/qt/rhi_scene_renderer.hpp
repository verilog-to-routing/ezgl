#pragma once

#include "ezgl/qt/rhi_types.hpp"

#include <QColor>
#include <QImage>
#include <QMatrix4x4>
#include <QSize>
#include <memory>
#include <vector>

// Forward-declare Qt RHI types to avoid pulling in the private RHI headers
// from every translation unit that includes this header.
QT_FORWARD_DECLARE_CLASS(QRhiBuffer)
QT_FORWARD_DECLARE_CLASS(QRhiCommandBuffer)
QT_FORWARD_DECLARE_CLASS(QRhiGraphicsPipeline)
QT_FORWARD_DECLARE_CLASS(QRhiRenderPassDescriptor)
QT_FORWARD_DECLARE_CLASS(QRhiRenderTarget)
QT_FORWARD_DECLARE_CLASS(QRhiSampler)
QT_FORWARD_DECLARE_CLASS(QRhiShaderResourceBindings)
QT_FORWARD_DECLARE_CLASS(QRhiTexture)
QT_FORWARD_DECLARE_CLASS(QRhi)

namespace ezgl {

/**
 * GPU pipeline state and per-frame resources for the RHI rendering backend.
 *
 * Owns all QRhi objects (PSOs, SRBs, uniform / vertex buffers, overlay
 * sampler) and the per-frame-slot geometry cache. Works with any QRhi
 * instance — the display path hands it the QRhiWidget's internal QRhi,
 * the headless path hands it a standalone QRhi built on QOffscreenSurface.
 *
 * Lifecycle:
 *   initialize(rhi, rp_desc)   — call once when QRhi and render-pass are ready
 *   render(cb, rt, ...)        — call every frame
 *   release()                  — call before the QRhi is destroyed
 */
class RhiSceneRenderer {
public:
    RhiSceneRenderer() = default;
    ~RhiSceneRenderer();

    // Non-copyable, non-movable (owns GPU resources).
    RhiSceneRenderer(const RhiSceneRenderer&)            = delete;
    RhiSceneRenderer& operator=(const RhiSceneRenderer&) = delete;

    /**
     * Create all GPU pipelines compatible with @p rp_desc.
     * Must be called before render(). Safe to call again after release().
     */
    void initialize(QRhi* rhi, QRhiRenderPassDescriptor* rp_desc);

    /**
     * Upload geometry / uniforms for @p frame_slot and record draw commands.
     *
     * @param cb            Command buffer in recording state.
     * @param rt            Render target to draw into.
     * @param pixel_size    Render target size in device pixels (for the viewport).
     * @param frame_slot    QRhi frame-in-flight slot index (0 for single-frame / headless).
     * @param geom_dirty    True if scene geometry has changed and must be re-uploaded.
     * @param scene         Geometry to render (may be nullptr if !geom_dirty).
     * @param mvp           World-to-NDC matrix.
     * @param visible_world Current visible world rectangle (for tile culling).
     * @param overlay       QPainter overlay image (text, arcs, …).
     * @param bg            Background clear colour.
     */
    void render(QRhiCommandBuffer*                         cb,
                QRhiRenderTarget*                          rt,
                const QSize&                               pixel_size,
                int                                        frame_slot,
                bool                                       geom_dirty,
                const std::shared_ptr<const SceneBuffers>& scene,
                const QMatrix4x4&                          mvp,
                const rectangle&                           visible_world,
                const QImage&                              overlay,
                QColor                                     bg);

    /** Destroy all GPU objects. Safe to call multiple times. */
    void release();

    bool is_initialized() const noexcept { return m_initialized; }

    /** Number of frame-in-flight slots allocated during initialize(). */
    int frame_count() const noexcept
    {
        return static_cast<int>(m_frame_resources.size());
    }

    /**
     * Mark all frame slots as needing geometry re-upload (e.g. after resize
     * or re-initialize).
     */
    void invalidate_geometry_cache();

private:
    // ---- GPU-side data structures (mirror the CPU-side SceneBuffers) --------

    struct GpuChunk {
        rectangle world_bounds;
        quint32   buffer_index = 0;
        quint32   byte_offset  = 0;
        quint32   count        = 0;
    };

    struct GpuStyleBuffer {
        StyleKey              style_key    = 0;
        std::uint32_t         rgba         = 0;
        quint32               style_offset = 0;
        std::vector<GpuChunk> chunks;
    };

    struct GpuSceneBuffers {
        std::vector<GpuStyleBuffer> thin_lines;
        std::vector<GpuStyleBuffer> fill_rects;
        std::vector<GpuStyleBuffer> fill_polys;
        std::vector<GpuStyleBuffer> thick_lines;
        std::vector<GpuStyleBuffer> dashed_lines;

        void clear()
        {
            thin_lines.clear(); fill_rects.clear(); fill_polys.clear();
            thick_lines.clear(); dashed_lines.clear();
        }
    };

    struct FrameResources {
        std::unique_ptr<QRhiBuffer>                 mvp_ubuf;
        std::unique_ptr<QRhiBuffer>                 style_ubuf;
        std::vector<std::unique_ptr<QRhiBuffer>>    thin_line_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>>    fill_rect_instance_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>>    fill_poly_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>>    thick_line_instance_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>>    dashed_line_instance_vbufs;
        std::unique_ptr<QRhiTexture>                overlay_tex;
        std::unique_ptr<QRhiShaderResourceBindings> overlay_srb;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
        GpuSceneBuffers                             gpu_scene;
    };

    // ---- state --------------------------------------------------------------

    QRhi*                                  m_rhi           = nullptr;
    bool                                   m_initialized   = false;

    // Pipelines
    std::unique_ptr<QRhiGraphicsPipeline>  m_line_pso;
    std::unique_ptr<QRhiGraphicsPipeline>  m_fill_rect_pso;
    std::unique_ptr<QRhiGraphicsPipeline>  m_fill_poly_pso;
    std::unique_ptr<QRhiGraphicsPipeline>  m_thick_line_pso;
    std::unique_ptr<QRhiGraphicsPipeline>  m_dashed_line_pso;
    std::unique_ptr<QRhiGraphicsPipeline>  m_overlay_pso;

    // Shared buffers (constant geometry, shared across all frame slots)
    std::unique_ptr<QRhiBuffer>            m_thick_line_corner_vbuf;
    std::unique_ptr<QRhiBuffer>            m_overlay_quad_vbuf;
    std::unique_ptr<QRhiSampler>           m_overlay_sampler;

    // Per-frame-slot resources
    std::vector<FrameResources>            m_frame_resources;
    std::vector<bool>                      m_frame_slot_geom_valid;

    // Latest complete scene — used to lazily re-upload to frame slots that
    // haven't yet received the current geometry revision.
    std::shared_ptr<const SceneBuffers>    m_cached_scene;
};

} // namespace ezgl
