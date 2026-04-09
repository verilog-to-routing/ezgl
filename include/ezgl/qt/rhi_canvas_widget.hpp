#pragma once

#if defined(EZGL_QT) && defined(EZGL_RHI)

#include "ezgl/rectangle.hpp"

#include <QRhiWidget>
#include <QImage>
#include <QMatrix4x4>
#include <QMutex>
#include <QColor>
#include <cstdint>
#include <memory>
#include <vector>
#include <functional>

// Forward-declare private Qt RHI types so we can hold them as unique_ptr members
// without pulling private headers into every translation unit.
QT_FORWARD_DECLARE_CLASS(QRhiBuffer)
QT_FORWARD_DECLARE_CLASS(QRhiShaderResourceBindings)
QT_FORWARD_DECLARE_CLASS(QRhiGraphicsPipeline)
QT_FORWARD_DECLARE_CLASS(QRhiSampler)
QT_FORWARD_DECLARE_CLASS(QRhiTexture)

namespace ezgl {

/**
 * Packed GPU vertex: world-space position only.
 * Layout matches the QRhiVertexInputAttribute setup in RhiCanvasWidget.
 *   Offset 0: float x, y   (8 bytes)
 */
struct PosVertex {
    float x, y;
};
static_assert(sizeof(PosVertex) == 8, "PosVertex must be 8 bytes");

/**
 * One corner of the thick-line quad template (4 total, constant buffer).
 *
 *   t    : 0.0 = at line start,  1.0 = at line end
 *   side : -1.0 = left edge,    +1.0 = right edge
 *
 * The same 4-corner buffer is reused for every thick-line draw call via
 * instanced rendering, so it never grows with the number of lines.
 */
struct QuadCorner {
    float t;
    float side;
};
static_assert(sizeof(QuadCorner) == 8, "QuadCorner must be 8 bytes");

/**
 * Per-instance data for one thick-line segment (instanced rendering).
 *
 * Memory cost: 20 bytes per line, vs 144 bytes with a per-vertex approach
 * (6 expanded vertices × 24 bytes).  For N lines: 7× less RAM.
 *
 *   Offset  0 : float x0, y0   — world-space start
 *   Offset  8 : float x1, y1   — world-space end
 *   Offset 16 : float width_px — full line width in screen pixels
 */
struct ThickLineInstance {
    float x0, y0;
    float x1, y1;
    float width_px;
};
static_assert(sizeof(ThickLineInstance) == 20, "ThickLineInstance must be 20 bytes");

/**
 * Per-instance data for one dashed-line segment (instanced rendering).
 *
 * Memory cost: 32 bytes per line. Same TriangleStrip quad as thick lines;
 * the fragment shader discards gap fragments using screen-pixel distance
 * while preserving phase continuity across tile-clipped segments.
 *
 *   Offset  0 : float x0, y0         — world-space clipped start
 *   Offset  8 : float x1, y1         — world-space clipped end
 *   Offset 16 : float width_px       — full line width in screen pixels (>= 1)
 *   Offset 20 : float dash_px        — dash run length in screen pixels
 *   Offset 24 : float gap_px         — gap length in screen pixels
 *   Offset 28 : float phase_world    — world-space distance from original segment start to x0/y0
 */
struct DashedLineInstance {
    float x0, y0;
    float x1, y1;
    float width_px;
    float dash_px;
    float gap_px;
    float phase_world;
};
static_assert(sizeof(DashedLineInstance) == 32, "DashedLineInstance must be 32 bytes");

// Compact style index per vertex. The fragment shader resolves it through a
// small palette UBO, avoiding one draw call per color run.
using StyleIndex = std::uint8_t;
static constexpr std::size_t kMaxRhiStyleEntries = 256;

struct RhiTileBatch {
    rectangle                    world_bounds;
    // Thin (1-pixel) lines — drawn with Lines topology.
    std::vector<PosVertex>       line_verts;
    std::vector<StyleIndex>      line_styles;
    // Filled rectangles — drawn with Triangles topology.
    std::vector<PosVertex>       fill_verts;
    std::vector<StyleIndex>      fill_styles;
    // draw_rectangle outlines (thin, 1-pixel) — drawn with Lines topology.
    std::vector<PosVertex>       draw_verts;
    std::vector<StyleIndex>      draw_styles;
    // Thick (width > 1 pixel) solid lines — instanced TriangleStrip, 21 bytes/line.
    std::vector<ThickLineInstance>  thick_line_instances;
    std::vector<StyleIndex>         thick_line_styles;
    // Dashed lines (any width) — instanced TriangleStrip, 33 bytes/line.
    std::vector<DashedLineInstance> dashed_line_instances;
    std::vector<StyleIndex>         dashed_line_styles;

    bool empty() const
    {
        return line_verts.empty()
            && fill_verts.empty()
            && draw_verts.empty()
            && thick_line_instances.empty()
            && dashed_line_instances.empty();
    }
};

/**
 * QWidget subclass (via QRhiWidget) that renders line and rect primitives on
 * the GPU (Vulkan / Metal / D3D12 / OpenGL via Qt RHI). The text/arc overlay
 * is uploaded as a texture and blended in a final full-screen quad pass inside
 * render(), avoiding QWidget-side compositing issues with QRhiWidget.
 *
 * Ownership model:
 *   - canvas::initialize() creates this widget in place of DrawingAreaWidget.
 *   - rhi_renderer calls set_frame_data() + update() each frame.
 *   - render() runs on Qt's render/GUI thread.
 */
class RhiCanvasWidget : public QRhiWidget {
    Q_OBJECT
public:
    explicit RhiCanvasWidget(QWidget* parent = nullptr);
    // Destructor defined in .cpp (QRhiBuffer etc. must be complete there).
    ~RhiCanvasWidget() override;

    /**
     * Push a full frame (geometry + transform).  Thread-safe.
     *
     * Vertex coordinates are in world space; the GPU applies world_to_ndc to
     * transform them. Call this when scene geometry has changed.
     *
     * @param tiles           Non-empty scene tiles, each with its own geometry streams.
     * @param palette_rgba    Packed RGBA palette referenced by the style indices.
     * @param world_to_ndc    Matrix mapping world coords → NDC.
     * @param visible_world   Current visible world bounds used for tile selection.
     * @param overlay         Transparent QImage with text / arcs drawn by QPainter.
     * @param bg_color        Clear color for the render target.
     */
    void set_frame_data(std::vector<RhiTileBatch> tiles,
                        std::vector<std::uint32_t> palette_rgba,
                        const QMatrix4x4&         world_to_ndc,
                        const rectangle&          visible_world,
                        const QImage&             overlay,
                        QColor                    bg_color);

    /**
     * Update only the camera transform (no geometry re-upload).  Thread-safe.
     *
     * Call this when the camera has panned or zoomed but no primitives changed.
     * The widget re-renders with the existing vertex buffers and the new MVP.
     */
    void set_mvp_only(const QMatrix4x4& world_to_ndc,
                      const rectangle&  visible_world);

    /** Register a callback invoked on every resize (mirrors DrawingAreaWidget). */
    void setResizeCallback(std::function<void(int,int)> cb);

    /** Pre-resize hook (lets canvas end its painter before the image is swapped). */
    void setPreResizeCallback(std::function<void()> cb);

protected:
    // QRhiWidget interface
    void initialize(QRhiCommandBuffer* cb) override;
    void render(QRhiCommandBuffer* cb) override;
    void releaseResources() override;

    void resizeEvent(QResizeEvent* e) override;
    void showEvent(QShowEvent* e) override;

private:
    struct StreamChunk {
        quint32 buffer_index = 0;
        quint32 pos_offset = 0;
        quint32 style_offset = 0;
        quint32 count = 0;
    };

    struct GpuTileBatch {
        rectangle                world_bounds;
        std::vector<StreamChunk> line_chunks;
        std::vector<StreamChunk> fill_chunks;
        std::vector<StreamChunk> draw_chunks;
        std::vector<StreamChunk> thick_line_chunks;
        std::vector<StreamChunk> dashed_line_chunks;
    };

    struct FrameResources {
        std::unique_ptr<QRhiBuffer>                 mvp_ubuf;
        std::unique_ptr<QRhiBuffer>                 palette_ubuf;
        std::vector<std::unique_ptr<QRhiBuffer>>    line_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>>    line_style_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>>    fill_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>>    fill_style_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>>    draw_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>>    draw_style_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>>    thick_line_instance_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>>    thick_line_style_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>>    dashed_line_instance_vbufs;
        std::vector<std::unique_ptr<QRhiBuffer>>    dashed_line_style_vbufs;
        std::unique_ptr<QRhiTexture>                overlay_tex;
        std::unique_ptr<QRhiShaderResourceBindings> overlay_srb;
        std::vector<GpuTileBatch>                   gpu_tiles;
        std::unique_ptr<QRhiShaderResourceBindings> srb;
    };

    // GPU resources — unique_ptr keeps them alive between initialize/release.
    // Mutable per-frame resources are duplicated across QRhi frame slots so
    // camera-only updates do not rewrite data that may still be in flight.
    std::vector<FrameResources>                m_frame_resources;
    // Thick-line instanced rendering:
    //   m_thick_line_corner_vbuf  — 4 QuadCorner values, immutable, shared by all draws.
    std::unique_ptr<QRhiBuffer>                m_thick_line_corner_vbuf;
    std::unique_ptr<QRhiBuffer>                m_overlay_quad_vbuf;
    std::unique_ptr<QRhiSampler>               m_overlay_sampler;
    std::unique_ptr<QRhiGraphicsPipeline>       m_line_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_fill_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_draw_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_thick_line_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_dashed_line_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_overlay_pso;
    bool m_initialized = false;

    // Pending frame (written by set_frame_data / set_mvp_only, consumed by render())
    mutable QMutex             m_frame_mutex;
    std::vector<RhiTileBatch>  m_pending_tiles;
    std::vector<std::uint32_t> m_pending_palette_rgba;
    QMatrix4x4                 m_pending_mvp;
    rectangle                  m_pending_visible_world;
    QImage                     m_pending_overlay;
    QColor                     m_pending_bg  { Qt::white };
    bool                       m_frame_dirty = false;  // geometry + MVP changed
    bool                       m_mvp_dirty   = false;  // only MVP changed

    // Cached tiled GPU streams for camera-only frames (no geometry re-upload).
    // One cache exists per QRhi frame slot.

    // Canvas hooks
    std::function<void(int,int)> m_resize_cb;
    std::function<void()>        m_pre_resize_cb;
};

} // namespace ezgl

#endif // EZGL_QT && EZGL_RHI
