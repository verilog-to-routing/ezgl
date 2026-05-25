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
 * @brief GPU pipeline state and per-frame resources for the rhi backend.
 *
 * Owns all @c QRhi objects: 7 graphics pipelines, shader resource
 * bindings, uniform/vertex buffers, overlay texture+sampler, and the
 * per-frame-slot geometry cache. Works with any @c QRhi instance — the
 * display path hands it the @c QRhiWidget's internal @c QRhi, the
 * headless path hands it a standalone @c QRhi built on
 * @c QOffscreenSurface.
 *
 * @par Pipelines (render order in render())
 * | # | Pipeline           | Topology / instancing                          | Shader pair                          |
 * | - | ------------------ | ---------------------------------------------- | ------------------------------------ |
 * | 1 | m_fill_rect_pso    | TriangleStrip, instanced                       | fill_rect.vert + base.frag           |
 * | 2 | m_fill_poly_pso    | Triangles                                      | base.vert + base.frag                |
 * | 3 | m_line_pso         | Lines                                          | base.vert + base.frag                |
 * | 4 | m_dashed_line_pso  | TriangleStrip, instanced quad (corner buf)     | dashed_line.vert + dashed_line.frag  |
 * | 5 | m_thick_line_pso   | TriangleStrip, instanced quad (corner buf)     | thick_line.vert + base.frag          |
 * | 6 | m_arrow_pso        | Triangles, 3 verts/instance via gl_VertexIndex | arrow.vert + base.frag               |
 * | 7 | m_overlay_pso      | TriangleStrip, one full-screen quad            | overlay.vert + overlay.frag (sampler)|
 *
 * @c base.vert is the minimal pass-through vertex shader
 * (@c vec2 inPosition → @c mvp * pos) shared by every pipeline whose
 * vertex stream is @ref PosVertex. @c base.frag writes the per-style
 * flat colour (@c fragColor = style.color) and is shared by every
 * pipeline whose only fragment output is that colour. The other
 * shaders are pipeline-specific.
 *
 * Render order is painter's-algorithm: fills first, then lines, then
 * arrows, then the QPainter overlay (text/arcs) composited on top.
 * Depth test/write disabled (2D). All pipelines use straight alpha blend
 * (SrcAlpha / OneMinusSrcAlpha) and call
 * @c setSampleCount(EZGL_RHI_SAMPLE_COUNT) so they match the render
 * target's MSAA configuration.
 *
 * @par UBO bindings (same layout across all shaders for SRB compatibility)
 * - binding 0 — @c mat4 mvp + @c vec2 viewport (per-frame, shared by all draws)
 * - binding 1 — @c vec4 color + @c vec4 line (per-style, dynamic-offset)
 *
 * Style UBO is one big buffer with one slot per unique @ref StyleKey,
 * written once per frame. Each draw binds the SRB with a
 * @c DynamicOffset pointing at its style's slot.
 *
 * @par Per-frame-slot resources
 * QRhi pipelines frames-in-flight (2–3 GPU frames overlap). Each slot
 * gets its own @ref FrameResources with separate buffers, SRBs, and
 * overlay texture. @c m_frame_slot_geom_valid tracks which slots already
 * hold the current geometry revision; lazy re-upload via
 * @c m_cached_scene keeps stale slots in sync without re-uploading
 * every frame.
 *
 * @par Lifecycle
 * - @ref initialize(rhi, rp_desc)   — call once when QRhi and render-pass are ready
 * - @ref render(cb, rt, ...)        — call every frame
 * - @ref release()                  — call before the QRhi is destroyed
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
        std::vector<GpuStyleBuffer> arrows;

        void clear()
        {
            thin_lines.clear(); fill_rects.clear(); fill_polys.clear();
            thick_lines.clear(); dashed_lines.clear(); arrows.clear();
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
        std::vector<std::unique_ptr<QRhiBuffer>>    arrow_instance_vbufs;
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
    std::unique_ptr<QRhiGraphicsPipeline>  m_arrow_pso;
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
