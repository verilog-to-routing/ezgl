#pragma once

#include "ezgl/irenderer.hpp"
#include "ezgl/qt/painter.hpp"

#include <QLineF>
#include <QPolygonF>
#include <QRectF>
#include <QFont>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ezgl {

// ---- style keys ----------------------------------------------------------

// Packed key for line primitives: color(32) | line_width(16) | line_cap(8) | line_dash(8)
struct LineStyleKey {
    uint32_t color_rgba;   // r | g<<8 | b<<16 | a<<24
    uint16_t line_width;   // current_line_width clamped to uint16
    uint8_t  line_cap;     // line_cap enum cast to uint8
    uint8_t  line_dash;    // line_dash enum cast to uint8

    uint64_t key() const {
        return uint64_t(color_rgba)
             | (uint64_t(line_width) << 32)
             | (uint64_t(line_cap)   << 48)
             | (uint64_t(line_dash)  << 56);
    }
};

// Packed key for filled rectangles: only color matters (no stroke attributes)
struct FillStyleKey {
    uint32_t color_rgba;

    uint64_t key() const { return color_rgba; }
};

// ---- batch storage -------------------------------------------------------

struct LineBatch {
    LineStyleKey        style;
    std::vector<QLineF> lines;
};

struct FillRectBatch {
    FillStyleKey        style;
    std::vector<QRectF> rects;
};

struct DrawRectBatch {
    LineStyleKey        style;
    std::vector<QRectF> rects;
};

struct FillPolyBatch {
    FillStyleKey            style;
    std::vector<QPolygonF>  polys;
};

struct DeferredPainterState {
    t_coordinate_system coordinate_system = WORLD;
    color               draw_color {0, 0, 0, 255};
    int                 line_width = 0;
    line_cap            line_cap_style = line_cap::butt;
    line_dash           line_dash_style = line_dash::none;
    double              rotation_radians = 0.0;
    justification       horiz_just = justification::center;
    justification       vert_just = justification::center;
    QFont               font;
};

struct DeferredPolyCommand {
    DeferredPainterState   state;
    std::vector<point2d>   points;
};

struct DeferredArcCommand {
    DeferredPainterState state;
    point2d              center;
    double               radius_x = 0.0;
    double               radius_y = 0.0;
    double               start_angle = 0.0;
    double               extent_angle = 0.0;
    bool                 fill = false;
};

struct DeferredTextCommand {
    DeferredPainterState state;
    point2d              point;
    std::string          text;
    double               bound_x = 0.0;
    double               bound_y = 0.0;
    bool                 scale_font_with_camera = false;
    double               recorded_world_scale = 1.0;
};

struct DeferredSurfaceCommand {
    DeferredPainterState state;
    surface*             p_surface = nullptr;
    point2d              anchor_point;
    double               scale_factor = 1.0;
};

using DeferredOverlayCommand =
    std::variant<DeferredPolyCommand,
                 DeferredArcCommand,
                 DeferredTextCommand,
                 DeferredSurfaceCommand>;

// ---- deferred_renderer ---------------------------------------------------

class deferred_renderer : public irenderer {
    const double MINIMAL_VISIBLE_TEXT_BOUND_Y_IN_PX = 5.0;
public:
    deferred_renderer(Painter *painter,
                      transform_fn transform,
                      camera *cam,
                      QImage *surface);

    ~deferred_renderer() override = default;

    // ---- irenderer: hot-path draw calls (batched) --------------------------

    void draw_line(const point2d& start, const point2d& end) override;

    void fill_rectangle(const point2d& start, const point2d& end) override;
    void fill_rectangle(const point2d& start, double width, double height) override;
    void fill_rectangle(const rectangle& r) override;

    void draw_rectangle(const point2d& start, const point2d& end) override;
    void draw_rectangle(const point2d& start, double width, double height) override;
    void draw_rectangle(const rectangle& r) override;

    // ---- irenderer: overlay draw calls (deferred to command queue) ---------

    void fill_poly(const std::vector<point2d>& points) override;
    void fill_triangle(const point2d& a, const point2d& b, const point2d& c) override;
    void draw_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    void draw_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;
    void fill_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    void fill_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;
    void draw_text(const point2d& point, std::string const& text) override;
    void draw_text(const point2d& point, std::string const& text,
                   double bound_x, double bound_y) override;
    void draw_surface(surface* p_surface, const point2d& anchor_point,
                      double scale_factor = 1) override;

    // ---- Flush all batches to the underlying QPainter, then reset ----------
    void flush();

    // ---- Methods used by rhi_renderer --------------------------------------

    // Replay stored overlay commands without resetting (for camera-only update).
    void replay_overlay();

    // Discard all stored commands and batches (called at begin of new frame).
    void clear_overlay_and_batches();

    // Redirect the internal painter to a new surface (called after resize).
    void set_painter_surface(Painter* painter, QImage* surface);

protected:
    void replay();
    void clear_deferred_primitives();

private:
    void ensure_overlay_index_grid();
    int clamp_overlay_tile_x(double x) const;
    int clamp_overlay_tile_y(double y) const;
    void index_world_overlay_command(std::uint32_t command_index,
                                     rectangle      bounds);
    void reset();
    DeferredPainterState capture_painter_state() const;
    void apply_painter_state(const DeferredPainterState& state);

    QRectF screen_viewport_rect() const;
    bool screen_rect_visible(const QRectF& rect, double padding = 0.0) const;
    bool screen_line_visible(const QLineF& line, double line_width) const;
    bool screen_poly_visible(const std::vector<point2d>& points) const;
    bool screen_arc_visible(const point2d& center,
                            double radius_x,
                            double radius_y) const;
    bool screen_text_visible(const point2d& point,
                             const std::string& text,
                             double bound_x,
                             double bound_y) const;
    bool screen_surface_visible(surface *p_surface,
                                const point2d& point,
                                double scale_factor) const;

    LineStyleKey current_line_style() const;
    FillStyleKey current_fill_style() const;

    void add_line(const LineStyleKey &s, QLineF line);
    void add_fill_rect(const FillStyleKey &s, QRectF rect);
    void add_draw_rect(const LineStyleKey &s, QRectF rect);
    void add_fill_poly(const FillStyleKey &s, QPolygonF poly);

    QRectF to_screen_rect(const point2d& start, const point2d& end);

    void push_arc_command(const point2d& center, double radius_x, double radius_y,
                          double start_angle, double extent_angle, bool fill);

    // Batch vectors — maintain submission order for painter's algorithm.
    std::vector<LineBatch>     m_line_batches;
    std::vector<FillRectBatch> m_fill_rect_batches;
    std::vector<DrawRectBatch> m_draw_rect_batches;
    std::vector<FillPolyBatch> m_fill_poly_batches;

    // Fast lookup: style key → index into the vectors above.
    std::unordered_map<uint64_t, size_t> m_line_idx;
    std::unordered_map<uint64_t, size_t> m_fill_rect_idx;
    std::unordered_map<uint64_t, size_t> m_draw_rect_idx;
    std::unordered_map<uint64_t, size_t> m_fill_poly_idx;
    std::vector<DeferredOverlayCommand>  m_overlay_commands;
    rectangle                            m_overlay_index_scene_bounds;
    double                               m_overlay_index_tile_width = 1.0;
    double                               m_overlay_index_tile_height = 1.0;
    std::vector<std::vector<std::uint32_t>> m_indexed_world_overlay_buckets;
    std::vector<std::uint32_t>           m_unindexed_overlay_commands;
    std::vector<std::uint32_t>           m_overlay_query_marks;
    std::uint32_t                        m_overlay_query_generation = 1;
};

} // namespace ezgl
