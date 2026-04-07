#pragma once

#if defined(EZGL_QT) && defined(EZGL_RHI)

#include "ezgl/graphics.hpp"
#include "ezgl/qt/painter.hpp"
#include "ezgl/qt/rhi_canvas_widget.hpp"

#include <QMatrix4x4>
#include <QImage>
#include <unordered_map>
#include <vector>

namespace ezgl {

/**
 * GPU-backed renderer (Phase 1+2).
 *
 * Hot-path primitives (lines, rectangles) are collected into position-only
 * vertex vectors plus compact style-index streams and submitted to the GPU via
 * RhiCanvasWidget.
 *
 * Non-overridden primitives (fill_poly, draw_arc, draw_text, draw_surface, …)
 * fall through to the renderer base class which draws them into m_overlay via
 * m_painter (QPainter → QImage).  The overlay is composited on top of the GPU
 * frame inside RhiCanvasWidget::paintEvent().
 *
 * Sub-pixel culling:
 *   Lines whose screen-projected length is < 0.4 px are skipped.
 */
class rhi_renderer : public renderer {
public:
    rhi_renderer(RhiCanvasWidget* widget,
                 transform_fn     transform,
                 camera*          cam,
                 QColor           bg_color);

    ~rhi_renderer() = default;

    // ---- Frame lifecycle ---------------------------------------------------

    /** Reset per-frame state (vertex buffers, overlay) ready for a new draw. */
    void begin_frame();

    // ---- Hot-path overrides -------------------------------------------------

    void draw_line(point2d start, point2d end) override;

    void fill_rectangle(point2d start, point2d end) override;
    void fill_rectangle(point2d start, double width, double height) override;
    void fill_rectangle(rectangle r) override;

    void draw_rectangle(point2d start, point2d end) override;
    void draw_rectangle(point2d start, double width, double height) override;
    void draw_rectangle(rectangle r) override;

    /**
     * Transfer all collected geometry to RhiCanvasWidget and schedule repaint.
     * Also ends the overlay painter so the QImage is fully flushed.
     */
    void flush();

    /**
     * Update only the camera MVP in RhiCanvasWidget (no geometry re-upload).
     * Call this after a pan/zoom when primitives have not changed.
     */
    void flush_mvp_only();

private:
    // ---- helpers ------------------------------------------------------------

    PosVertex make_vertex(point2d world_pt) const;
    std::uint32_t current_packed_color() const;
    StyleIndex current_style_index();
    void append_style_indices(std::vector<StyleIndex>& styles,
                              StyleIndex               style_index,
                              std::size_t              count);

    /** Transform a world/screen point to screen-pixel coords. */
    point2d to_screen(point2d p) const;

    /** Compute screen→NDC orthographic matrix from current widget size. */
    QMatrix4x4 compute_mvp() const;

    /** Append 6 triangle vertices for a filled rect (two CCW triangles). */
    void push_fill_rect(point2d tl, point2d br);

    /** Append 8 line vertices for an outline rect (4 line segments). */
    void push_draw_rect(point2d tl, point2d br);

    // ---- state --------------------------------------------------------------

    RhiCanvasWidget*         m_rhi_widget;
    QColor                   m_bg_color;

    // GPU geometry collections (world coords) + palette-backed style indices.
    std::vector<PosVertex>   m_lines;
    std::vector<StyleIndex>  m_line_styles;
    std::vector<PosVertex>   m_fill_verts;
    std::vector<StyleIndex>  m_fill_styles;
    std::vector<PosVertex>   m_draw_verts;
    std::vector<StyleIndex>  m_draw_styles;
    std::vector<std::uint32_t> m_palette_rgba;
    std::unordered_map<std::uint32_t, StyleIndex> m_palette_index;

    // QPainter overlay — base-class draw calls (text, arcs, …) write here.
    QImage   m_overlay;
    Painter  m_overlay_painter;   // must be declared AFTER m_overlay
};

} // namespace ezgl

#endif // EZGL_QT && EZGL_RHI
