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
 *
 * @note New to the graphics acronyms here (UBO, VBO, SRB, PSO, MSAA, …)? They
 *       are defined once in the glossary at the top of @ref rhi_types.hpp.
 */
class RhiSceneRenderer {
public:
    /// Construct an idle renderer holding no GPU objects. No @c QRhi is
    /// touched until @ref initialize() is called, so this is safe to build
    /// before any graphics context exists.
    RhiSceneRenderer() = default;
    /// Release every GPU object via @ref release() before destruction. Safe
    /// even if @ref initialize() was never called or @ref release() already ran.
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
     * @par State-change rate
     * The render loop is structured for the minimum possible GPU state
     * change rate: each pipeline (PSO) is bound **once per frame** (for
     * its primitive type's entire batch list); the per-style SRB
     * (carrying the @c style_ubuf color slot) is bound **once per
     * style key**; @c cmdDraw is issued **only for chunks whose
     * world_bounds intersects @p visible_world**. Non-visible chunks
     * cost a CPU AABB test and nothing more.
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

    /// True once @ref initialize() has built the pipelines and before
    /// @ref release() tears them down; guards callers from issuing @ref render()
    /// against a renderer with no GPU objects.
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

    /// GPU-side counterpart of a CPU @ref Chunk: one drawable sub-range of an
    /// uploaded style VBO. Unlike the CPU @ref Chunk (which indexes a single
    /// flat array by element @c offset/count), a chunk here names *which*
    /// per-slot VBO holds it (@c buffer_index into the matching
    /// @c FrameResources vector) plus a @c byte_offset into that buffer, since
    /// a style's geometry may be split across several GPU buffers. Retains
    /// @c world_bounds so @ref render() culls the chunk against the visible
    /// world before emitting a @c cmdDraw.
    struct GpuChunk {
        rectangle world_bounds;   ///< Tile cell bounds — tested against the visible world rect.
        quint32   buffer_index = 0; ///< Index of the owning VBO within its per-type @c FrameResources vector.
        quint32   byte_offset  = 0; ///< Byte offset of this chunk's first vertex/instance within that VBO.
        quint32   count        = 0; ///< Number of vertices/instances to draw.
    };

    /// GPU-side counterpart of @ref StyleBufferCommon: all chunks of one
    /// primitive type sharing one @ref StyleKey. @c style_offset is the byte
    /// offset of this style's slot in the shared @c style_ubuf, bound as a
    /// @c DynamicOffset for every draw in @c chunks; @c rgba is cached here so
    /// the UBO can be filled without re-decoding the key.
    struct GpuStyleBuffer {
        StyleKey              style_key    = 0; ///< Key uniquely identifying this style batch.
        std::uint32_t         rgba         = 0; ///< Unpacked colour written into the style UBO slot.
        quint32               style_offset = 0; ///< Dynamic-offset of this style's slot in @c style_ubuf.
        std::vector<GpuChunk> chunks;           ///< Per-tile drawable ranges for this style.
    };

    /// GPU-side mirror of @ref SceneBuffers: the per-frame-slot draw plan,
    /// grouped by primitive type and, within each type, by @ref StyleKey.
    /// Rebuilt from the CPU @ref SceneBuffers when a slot's geometry is
    /// (re-)uploaded; drives the draw loop in @ref render().
    struct GpuSceneBuffers {
        std::vector<GpuStyleBuffer> thin_lines;   ///< Thin-line style batches.
        std::vector<GpuStyleBuffer> fill_rects;   ///< Filled-rectangle style batches.
        std::vector<GpuStyleBuffer> fill_polys;   ///< Filled-polygon style batches.
        std::vector<GpuStyleBuffer> thick_lines;  ///< Thick-line style batches.
        std::vector<GpuStyleBuffer> dashed_lines; ///< Dashed-line style batches.
        std::vector<GpuStyleBuffer> arrows;       ///< Arrow-head style batches.

        /// Drop every style batch of every type, leaving an empty draw plan.
        void clear()
        {
            thin_lines.clear(); fill_rects.clear(); fill_polys.clear();
            thick_lines.clear(); dashed_lines.clear(); arrows.clear();
        }
    };

    /// All GPU objects owned by one frame-in-flight slot. QRhi overlaps 2–3
    /// GPU frames, so each slot keeps its own buffers, SRBs, and overlay
    /// texture — this lets the CPU record frame N+1 while the GPU still reads
    /// frame N's resources without hazards. One vector of VBOs per primitive
    /// type because a single style's geometry may span several buffers (see
    /// @ref GpuChunk::buffer_index).
    struct FrameResources {
        std::unique_ptr<QRhiBuffer>                 mvp_ubuf;   ///< Per-frame UBO: mvp matrix + viewport (binding 0).
        std::unique_ptr<QRhiBuffer>                 style_ubuf; ///< Per-style UBO: one colour/line slot per StyleKey (binding 1).
        std::vector<std::unique_ptr<QRhiBuffer>>    thin_line_vbufs;            ///< Thin-line vertex buffers.
        std::vector<std::unique_ptr<QRhiBuffer>>    fill_rect_instance_vbufs;   ///< Filled-rect instance buffers.
        std::vector<std::unique_ptr<QRhiBuffer>>    fill_poly_vbufs;            ///< Filled-poly vertex buffers.
        std::vector<std::unique_ptr<QRhiBuffer>>    thick_line_instance_vbufs;  ///< Thick-line instance buffers.
        std::vector<std::unique_ptr<QRhiBuffer>>    dashed_line_instance_vbufs; ///< Dashed-line instance buffers.
        std::vector<std::unique_ptr<QRhiBuffer>>    arrow_instance_vbufs;       ///< Arrow instance buffers.
        std::unique_ptr<QRhiTexture>                overlay_tex; ///< This slot's copy of the QPainter overlay image.
        std::unique_ptr<QRhiShaderResourceBindings> overlay_srb; ///< SRB binding @c overlay_tex + sampler for the overlay pass.
        std::unique_ptr<QRhiShaderResourceBindings> srb;         ///< SRB for scene draws (mvp UBO + dynamic-offset style UBO).
        GpuSceneBuffers                             gpu_scene;   ///< This slot's uploaded draw plan.
    };

    // ---- state --------------------------------------------------------------

    QRhi*                                  m_rhi           = nullptr; ///< Borrowed QRhi (owned by the widget/headless path); not deleted here.
    bool                                   m_initialized   = false;   ///< True between @ref initialize() and @ref release().

    // Pipelines — one graphics PSO per primitive type; see the class-level
    // pipeline table for topology/instancing and shader pairs.
    std::unique_ptr<QRhiGraphicsPipeline>  m_line_pso;        ///< Thin (1-pixel) lines.
    std::unique_ptr<QRhiGraphicsPipeline>  m_fill_rect_pso;   ///< Instanced filled rectangles.
    std::unique_ptr<QRhiGraphicsPipeline>  m_fill_poly_pso;   ///< Triangulated filled polygons.
    std::unique_ptr<QRhiGraphicsPipeline>  m_thick_line_pso;  ///< Instanced screen-width lines.
    std::unique_ptr<QRhiGraphicsPipeline>  m_dashed_line_pso; ///< Instanced dashed lines.
    std::unique_ptr<QRhiGraphicsPipeline>  m_arrow_pso;       ///< Instanced arrow heads.
    std::unique_ptr<QRhiGraphicsPipeline>  m_overlay_pso;     ///< Full-screen textured quad compositing the QPainter overlay.

    // Shared buffers (constant geometry, shared across all frame slots)
    std::unique_ptr<QRhiBuffer>            m_thick_line_corner_vbuf; ///< Unit quad corners expanded per thick/dashed instance in the vertex shader.
    std::unique_ptr<QRhiBuffer>            m_overlay_quad_vbuf;      ///< Full-screen quad for the overlay pass.
    std::unique_ptr<QRhiSampler>           m_overlay_sampler;        ///< Sampler for the overlay texture.

    // Per-frame-slot resources.
    //
    // Why several slots: the GPU renders asynchronously, so while it is still
    // reading a frame's buffers the CPU is already recording the next frame.
    // QRhi lets 2-3 frames overlap this way ("frames in flight"). If both
    // frames shared one set of buffers, the CPU would overwrite data the GPU
    // is mid-read — so each in-flight frame gets its own slot of buffers,
    // textures, and SRBs, and they are cycled round-robin by @c frame_slot.
    std::vector<FrameResources>            m_frame_resources;       ///< One @ref FrameResources per frame-in-flight slot.
    std::vector<bool>                      m_frame_slot_geom_valid; ///< Per-slot flag: true when the slot holds the current geometry revision.

    /// The most recent scene geometry. Kept around because each frame slot has
    /// its own GPU buffers, so a new scene has to be uploaded to every slot.
    /// Rather than upload to all slots at once, we upload only to the slot being
    /// drawn this frame and mark the others stale (@c m_frame_slot_geom_valid).
    /// A stale slot re-uploads from this cached copy the next time it is used.
    std::shared_ptr<const SceneBuffers>    m_cached_scene;
};

} // namespace ezgl
