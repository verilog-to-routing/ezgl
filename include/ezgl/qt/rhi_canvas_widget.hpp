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
 * GPU vertex for thick (width > 0) lines.
 *
 * The thick_line.vert shader uses the MVP + viewport uniforms to expand each
 * vertex perpendicularly by half the requested pixel width at render time, so
 * the geometry does not need to be regenerated when the camera pans or zooms.
 *
 * Layout (all floats, 24 bytes total):
 *   Offset  0 : float x, y       — world-space endpoint (start or end)
 *   Offset  8 : float perp_x, y  — normalized world-space perpendicular unit vector
 *   Offset 16 : float width_px   — full line width in screen pixels
 *   Offset 20 : float side       — +1.0 (one edge) or -1.0 (other edge)
 */
struct ThickLineVertex {
    float x, y;
    float perp_x, perp_y;
    float width_px;
    float side;
};
static_assert(sizeof(ThickLineVertex) == 24, "ThickLineVertex must be 24 bytes");

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
    // Thick (width > 1 pixel) lines and rectangle outlines — drawn with
    // Triangles topology via thick_line.vert shader (6 verts per segment).
    std::vector<ThickLineVertex> thick_line_verts;
    std::vector<StyleIndex>      thick_line_styles;

    bool empty() const
    {
        return line_verts.empty()
            && fill_verts.empty()
            && draw_verts.empty()
            && thick_line_verts.empty();
    }
};

/**
 * QWidget subclass (via QRhiWidget) that renders line and rect primitives on
 * the GPU (Vulkan / Metal / D3D12 / OpenGL via Qt RHI), then composites a
 * QPainter overlay (text, arcs) on top.
 *
 * Ownership model:
 *   - canvas::initialize() creates this widget in place of DrawingAreaWidget.
 *   - rhi_renderer calls set_frame_data() + update() each frame.
 *   - render() and paintEvent() run on Qt's render/GUI thread.
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

    // Override paintEvent to composite the QPainter overlay after the GPU blit.
    void paintEvent(QPaintEvent* e) override;
    void resizeEvent(QResizeEvent* e) override;

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
    };

    // GPU resources — unique_ptr keeps them alive between initialize/release.
    // Complete type required only in .cpp.
    std::unique_ptr<QRhiBuffer>                 m_mvp_ubuf;
    std::unique_ptr<QRhiBuffer>                 m_palette_ubuf;
    std::vector<std::unique_ptr<QRhiBuffer>>    m_line_vbufs;
    std::vector<std::unique_ptr<QRhiBuffer>>    m_line_style_vbufs;
    std::vector<std::unique_ptr<QRhiBuffer>>    m_fill_vbufs;
    std::vector<std::unique_ptr<QRhiBuffer>>    m_fill_style_vbufs;
    std::vector<std::unique_ptr<QRhiBuffer>>    m_draw_vbufs;
    std::vector<std::unique_ptr<QRhiBuffer>>    m_draw_style_vbufs;
    // Thick-line position buffers store ThickLineVertex (24 bytes each).
    std::vector<std::unique_ptr<QRhiBuffer>>    m_thick_line_vbufs;
    std::vector<std::unique_ptr<QRhiBuffer>>    m_thick_line_style_vbufs;
    std::vector<GpuTileBatch>                   m_gpu_tiles;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline>       m_line_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_fill_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_draw_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_thick_line_pso;
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

    // Canvas hooks
    std::function<void(int,int)> m_resize_cb;
    std::function<void()>        m_pre_resize_cb;
};

} // namespace ezgl

#endif // EZGL_QT && EZGL_RHI
