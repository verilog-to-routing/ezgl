#pragma once

#if defined(EZGL_QT) && defined(EZGL_RHI)

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
 * Draw range with a single RGBA color.
 *
 * first/count address a contiguous run inside one vertex buffer. The color is
 * packed as r | g<<8 | b<<16 | a<<24 to match ezgl::color storage elsewhere.
 */
struct ColorBatch {
    std::uint32_t first      = 0;
    std::uint32_t count      = 0;
    std::uint32_t color_rgba = 0;
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
     * @param lines         Two PosVertex entries per line segment (world coords).
     * @param line_batches  Contiguous line runs that share one color each.
     * @param fill_verts    Six PosVertex entries per filled rectangle (world coords).
     * @param fill_batches  Contiguous fill-rect runs that share one color each.
     * @param draw_verts    Eight PosVertex entries per outline rectangle (world coords).
     * @param draw_batches  Contiguous outline-rect runs that share one color each.
     * @param world_to_ndc  Matrix mapping world coords → NDC.
     * @param overlay     Transparent QImage with text / arcs drawn by QPainter.
     * @param bg_color    Clear color for the render target.
     */
    void set_frame_data(std::vector<PosVertex>   lines,
                        std::vector<ColorBatch>  line_batches,
                        std::vector<PosVertex>   fill_verts,
                        std::vector<ColorBatch>  fill_batches,
                        std::vector<PosVertex>   draw_verts,
                        std::vector<ColorBatch>  draw_batches,
                        const QMatrix4x4&       world_to_ndc,
                        const QImage&           overlay,
                        QColor                  bg_color);

    /**
     * Update only the camera transform (no geometry re-upload).  Thread-safe.
     *
     * Call this when the camera has panned or zoomed but no primitives changed.
     * The widget re-renders with the existing vertex buffers and the new MVP.
     */
    void set_mvp_only(const QMatrix4x4& world_to_ndc);

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
    // GPU resources — unique_ptr keeps them alive between initialize/release.
    // Complete type required only in .cpp.
    std::unique_ptr<QRhiBuffer>                 m_mvp_ubuf;
    std::unique_ptr<QRhiBuffer>                 m_color_ubuf;
    std::unique_ptr<QRhiBuffer>                 m_line_vbuf;
    std::unique_ptr<QRhiBuffer>                 m_fill_vbuf;
    std::unique_ptr<QRhiBuffer>                 m_draw_vbuf;
    std::unique_ptr<QRhiShaderResourceBindings> m_srb;
    std::unique_ptr<QRhiGraphicsPipeline>       m_line_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_fill_pso;
    std::unique_ptr<QRhiGraphicsPipeline>       m_draw_pso;
    bool m_initialized = false;

    // Pending frame (written by set_frame_data / set_mvp_only, consumed by render())
    mutable QMutex           m_frame_mutex;
    std::vector<PosVertex>   m_pending_lines;
    std::vector<ColorBatch>  m_pending_line_batches;
    std::vector<PosVertex>   m_pending_fill;
    std::vector<ColorBatch>  m_pending_fill_batches;
    std::vector<PosVertex>   m_pending_draw;
    std::vector<ColorBatch>  m_pending_draw_batches;
    QMatrix4x4               m_pending_mvp;
    QImage                   m_pending_overlay;
    QColor                   m_pending_bg  { Qt::white };
    bool                     m_frame_dirty = false;  // geometry + MVP changed
    bool                     m_mvp_dirty   = false;  // only MVP changed

    // Cached batch state for camera-only frames (no geometry re-upload).
    std::vector<ColorBatch>  m_line_batches;
    std::vector<ColorBatch>  m_fill_batches;
    std::vector<ColorBatch>  m_draw_batches;
    quint32                  m_color_ubuf_stride = 0;

    // Canvas hooks
    std::function<void(int,int)> m_resize_cb;
    std::function<void()>        m_pre_resize_cb;
};

} // namespace ezgl

#endif // EZGL_QT && EZGL_RHI
