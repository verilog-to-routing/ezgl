#pragma once

#if defined(EZGL_QT) && defined(EZGL_RHI)

#include <QRhiWidget>
#include <QImage>
#include <QMatrix4x4>
#include <QMutex>
#include <QColor>
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
 * Packed GPU vertex: screen-space pixel coords (float32) + RGBA color (uint8×4).
 * Layout matches the QRhiVertexInputAttribute setup in RhiCanvasWidget.
 *   Offset 0: float x, y   (8 bytes)
 *   Offset 8: uint8 r,g,b,a (4 bytes) — uploaded as UNormByte4
 * Total: 12 bytes per vertex.
 */
struct LineVertex {
    float   x, y;
    uint8_t r, g, b, a;
};
static_assert(sizeof(LineVertex) == 12, "LineVertex must be 12 bytes");

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
     * @param lines       Two LineVertex entries per line segment (world coords).
     * @param fill_verts  Six LineVertex entries per filled rectangle (world coords).
     * @param draw_verts  Eight LineVertex entries per outline rectangle (world coords).
     * @param world_to_ndc  Matrix mapping world coords → NDC.
     * @param overlay     Transparent QImage with text / arcs drawn by QPainter.
     * @param bg_color    Clear color for the render target.
     */
    void set_frame_data(std::vector<LineVertex> lines,
                        std::vector<LineVertex> fill_verts,
                        std::vector<LineVertex> draw_verts,
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
    std::unique_ptr<QRhiBuffer>                 m_ubuf;
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
    std::vector<LineVertex>  m_pending_lines;
    std::vector<LineVertex>  m_pending_fill;
    std::vector<LineVertex>  m_pending_draw;
    QMatrix4x4               m_pending_mvp;
    QImage                   m_pending_overlay;
    QColor                   m_pending_bg  { Qt::white };
    bool                     m_frame_dirty = false;  // geometry + MVP changed
    bool                     m_mvp_dirty   = false;  // only MVP changed

    // Cached vertex counts for camera-only frames (no geometry re-upload).
    quint32                  m_line_count  = 0;
    quint32                  m_fill_count  = 0;
    quint32                  m_draw_count  = 0;

    // Canvas hooks
    std::function<void(int,int)> m_resize_cb;
    std::function<void()>        m_pre_resize_cb;
};

} // namespace ezgl

#endif // EZGL_QT && EZGL_RHI
