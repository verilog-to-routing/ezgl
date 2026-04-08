#pragma once

#if defined(EZGL_QT) && defined(EZGL_RHI)

#include "ezgl/graphics.hpp"
#include "ezgl/qt/painter.hpp"
#include "ezgl/qt/rhi_canvas_widget.hpp"

#include <QMatrix4x4>
#include <QImage>
#include <cstddef>
#include <unordered_map>
#include <vector>

namespace ezgl {

/**
 * GPU-backed renderer with scene tiling.
 *
 * Hot-path primitives (lines, rectangles) are clipped into a fixed 256 x 256
 * grid over the scene bounds. Each non-empty tile carries its own geometry
 * streams and is submitted to RhiCanvasWidget as an independent GPU batch.
 *
 * Non-overridden primitives (fill_poly, draw_arc, draw_text, draw_surface, …)
 * fall through to the renderer base class which draws them into m_overlay via
 * m_painter (QPainter → QImage).  The overlay is composited on top of the GPU
 * frame inside RhiCanvasWidget::paintEvent().
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
    static constexpr int kTileGridDimension = 256;

    // ---- helpers ------------------------------------------------------------

    PosVertex make_vertex(point2d world_pt) const;
    std::uint32_t current_packed_color() const;
    StyleIndex current_style_index();
    void append_segment(std::vector<PosVertex>& verts,
                        std::vector<StyleIndex>& styles,
                        point2d                  start,
                        point2d                  end,
                        StyleIndex               style_index);
    void append_fill_rect(RhiTileBatch& tile,
                          point2d       p0,
                          point2d       p1,
                          StyleIndex    style_index);
    void append_line_to_tiles(point2d start,
                              point2d end,
                              StyleIndex style_index);
    void append_draw_segment_to_tiles(point2d start,
                                      point2d end,
                                      StyleIndex style_index);
    void append_fill_rect_to_tiles(point2d p0,
                                   point2d p1,
                                   StyleIndex style_index);

    // Thick line helpers (line_width > 0).
    // Appends one ThickLineInstance per clipped segment (instanced rendering).
    void append_thick_segment(RhiTileBatch& tile,
                              point2d       start,
                              point2d       end,
                              float         width_px,
                              StyleIndex    style_index);
    void append_thick_line_to_tiles(point2d    start,
                                    point2d    end,
                                    float      width_px,
                                    StyleIndex style_index);
    void append_thick_draw_segment_to_tiles(point2d    start,
                                            point2d    end,
                                            float      width_px,
                                            StyleIndex style_index);

    // Dashed line helpers.
    // Appends one DashedLineInstance per clipped segment.
    void append_dashed_segment(RhiTileBatch& tile,
                               point2d       start,
                               point2d       end,
                               float         width_px,
                               float         dash_px,
                               float         gap_px,
                               StyleIndex    style_index);
    void append_dashed_line_to_tiles(point2d    start,
                                     point2d    end,
                                     float      width_px,
                                     float      dash_px,
                                     float      gap_px,
                                     StyleIndex style_index);
    void append_dashed_draw_segment_to_tiles(point2d    start,
                                             point2d    end,
                                             float      width_px,
                                             float      dash_px,
                                             float      gap_px,
                                             StyleIndex style_index);
    void set_dash_pattern(float width_px,
                          float& dash_px,
                          float& gap_px) const;
    void ensure_tile_grid();
    void clear_tile_geometry();
    int clamp_tile_x(double x) const;
    int clamp_tile_y(double y) const;
    int tile_index(int tile_x, int tile_y) const;
    RhiTileBatch& tile_at(int tile_x, int tile_y);

    /** Compute screen→NDC orthographic matrix from current widget size. */
    QMatrix4x4 compute_mvp() const;

    // ---- state --------------------------------------------------------------

    RhiCanvasWidget*         m_rhi_widget;
    QColor                   m_bg_color;

    // Scene tiling metadata and CPU-side tile batches.
    rectangle                m_scene_bounds;
    double                   m_tile_width = 1.0;
    double                   m_tile_height = 1.0;
    std::vector<RhiTileBatch> m_tiles;

    // Global palette shared by every tile for the frame.
    std::vector<std::uint32_t> m_palette_rgba;
    std::unordered_map<std::uint32_t, StyleIndex> m_palette_index;

    // QPainter overlay — base-class draw calls (text, arcs, …) write here.
    QImage   m_overlay;
    Painter  m_overlay_painter;   // must be declared AFTER m_overlay
};

} // namespace ezgl

#endif // EZGL_QT && EZGL_RHI
