#pragma once

#include "ezgl/color.hpp"
#include "ezgl/point.hpp"
#include "ezgl/rectangle.hpp"

#include <QFont>
#include <Qt>
#include <QImage>

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace ezgl {

typedef QImage surface;

class camera;
class Painter;

enum t_coordinate_system { WORLD, SCREEN };

enum class justification { center, left, right, top, bottom };

enum class font_slant : int {
    normal  = QFont::StyleNormal,
    italic  = QFont::StyleItalic,
    oblique = QFont::StyleOblique
};

enum class font_weight : int {
    normal = QFont::Normal,
    bold   = QFont::Bold
};

enum class line_cap : int {
    butt  = Qt::FlatCap,
    round = Qt::RoundCap
};

enum class line_dash : int {
    none,
    asymmetric_5_3
};

/**
 * Base interface and shared state for all ezgl renderers.
 *
 * Shared state setters and camera helpers live here. Concrete renderers own
 * the draw-call behavior they batch, accelerate, or paint immediately.
 */
class irenderer {
public:
    using transform_fn = std::function<point2d(point2d)>;

    virtual ~irenderer() = default;

    virtual void set_coordinate_system(t_coordinate_system new_coordinate_system);
    virtual void set_visible_world(rectangle new_world);
    virtual rectangle get_visible_world();
    virtual rectangle get_visible_screen() const;
    virtual rectangle world_to_screen(const rectangle& box);

    virtual void set_color(color new_color);
    virtual void set_color(color new_color, uint_fast8_t alpha);
    virtual void set_color(uint_fast8_t red, uint_fast8_t green, uint_fast8_t blue,
                           uint_fast8_t alpha = 255);
    virtual void set_line_cap(line_cap cap);
    virtual void set_line_dash(line_dash dash);
    virtual void set_line_width(int width);
    virtual void set_font_size(double new_size);
    virtual void format_font(std::string const& family, font_slant slant, font_weight weight);
    virtual void format_font(std::string const& family, font_slant slant,
                             font_weight weight, double new_size);
    virtual void set_text_rotation(double degrees);
    virtual void set_horiz_justification(justification horiz_just);
    virtual void set_vert_justification(justification vert_just);

    /**
     * Set a one-shot screen-pixel offset to be applied to the next
     * draw_text call. The offset is added AFTER the world→screen
     * transform, so its visible distance is constant in screen pixels at
     * every zoom level — useful for placing labels just off a line drawn
     * in WORLD coords (e.g. critical-path delay annotations) without the
     * label drifting on zoom under the camera-only redraw path.
     *
     * The offset auto-resets to (0,0) once consumed by the next draw_text.
     */
    virtual void set_text_screen_offset(point2d offset_px);

    virtual void draw_line(const point2d& start, const point2d& end) = 0;
    virtual void draw_rectangle(const point2d& start, const point2d& end) = 0;
    virtual void draw_rectangle(const point2d& start, double width, double height) = 0;
    virtual void draw_rectangle(const rectangle& r) = 0;
    virtual void fill_rectangle(const point2d& start, const point2d& end) = 0;
    virtual void fill_rectangle(const point2d& start, double width, double height) = 0;
    virtual void fill_rectangle(const rectangle& r) = 0;
    virtual void fill_poly(const std::vector<point2d>& points) = 0;
    virtual void fill_triangle(const point2d& a, const point2d& b, const point2d& c) = 0;
    virtual void draw_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                                   double start_angle, double extent_angle) = 0;
    virtual void draw_arc(const point2d& center, double radius,
                          double start_angle, double extent_angle) = 0;
    virtual void fill_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                                   double start_angle, double extent_angle) = 0;
    virtual void fill_arc(const point2d& center, double radius,
                          double start_angle, double extent_angle) = 0;
    virtual void draw_text(const point2d& point, std::string const& text) = 0;
    virtual void draw_text(const point2d& point, std::string const& text,
                           double bound_x, double bound_y) = 0;
    virtual void draw_surface(surface* p_surface, const point2d& anchor_point,
                              double scale_factor = 1) = 0;

    static surface* load_png(const char* file_path);
    static void free_surface(surface* p_surface);

protected:
    irenderer(Painter* painter, transform_fn transform, camera* cam, QImage* surface);

    Painter*            m_painter{nullptr};
    transform_fn        m_transform;
    camera*             m_camera{nullptr};
    t_coordinate_system current_coordinate_system = WORLD;
    color               current_color{0, 0, 0, 255};
    int                 current_line_width  = 0;
    line_cap            current_line_cap    = line_cap::butt;
    line_dash           current_line_dash   = line_dash::none;
    double              rotation_angle      = 0.0;
    justification       horiz_justification = justification::center;
    justification       vert_justification  = justification::center;
    QFont               current_font;
    point2d             text_screen_offset_px = {0.0, 0.0};

    void update_painter(Painter* painter, QImage* surface);

    bool rectangle_off_screen(rectangle rect);
    bool clip_line_world(const rectangle& clip_window, point2d& start, point2d& end);
    void paint_line(const point2d& start, const point2d& end);
    void paint_rectangle_path(const point2d& start, const point2d& end, bool fill);
    void paint_poly(const std::vector<point2d>& points);
    void paint_arc_path(const point2d& center, double radius, double start_angle,
                       double extent_angle, double stretch_factor, bool fill);
    void paint_text(const point2d& point, const std::string& text,
                    double bound_x, double bound_y);
    void paint_surface(surface* p_surface, const point2d& anchor, double scale_factor);
};

using renderer = irenderer;

} // namespace ezgl
