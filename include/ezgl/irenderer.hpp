#pragma once

#include "ezgl/color.hpp"
#include "ezgl/point.hpp"
#include "ezgl/rectangle.hpp"

#include <QFont>
#include <Qt>
#include <QImage>

#include <cstdint>
#include <string>
#include <vector>

namespace ezgl {

typedef QImage surface;

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
 * Pure abstract interface for all ezgl renderers.
 *
 * The public drawing API (set_color, draw_line, …) is expressed as pure virtual
 * methods so that canvas can dispatch to any concrete renderer at runtime without
 * knowing its type.
 */
class irenderer {
public:
    virtual ~irenderer() = default;

    virtual void set_coordinate_system(t_coordinate_system new_coordinate_system) = 0;
    virtual void set_visible_world(rectangle new_world) = 0;
    virtual rectangle get_visible_world() = 0;
    virtual rectangle get_visible_screen() const = 0;
    virtual rectangle world_to_screen(const rectangle& box) = 0;

    virtual void set_color(color new_color) = 0;
    virtual void set_color(color new_color, uint_fast8_t alpha) = 0;
    virtual void set_color(uint_fast8_t red, uint_fast8_t green, uint_fast8_t blue, uint_fast8_t alpha = 255) = 0;
    virtual void set_line_cap(line_cap cap) = 0;
    virtual void set_line_dash(line_dash dash) = 0;
    virtual void set_line_width(int width) = 0;
    virtual void set_font_size(double new_size) = 0;
    virtual void format_font(std::string const& family, font_slant slant, font_weight weight) = 0;
    virtual void format_font(std::string const& family, font_slant slant, font_weight weight, double new_size) = 0;
    virtual void set_text_rotation(double degrees) = 0;
    virtual void set_horiz_justification(justification horiz_just) = 0;
    virtual void set_vert_justification(justification vert_just) = 0;

    virtual void draw_line(const point2d& start, const point2d& end) = 0;
    virtual void draw_rectangle(const point2d& start, const point2d& end) = 0;
    virtual void draw_rectangle(const point2d& start, double width, double height) = 0;
    virtual void draw_rectangle(rectangle r) = 0;
    virtual void fill_rectangle(const point2d& start, const point2d& end) = 0;
    virtual void fill_rectangle(const point2d& start, double width, double height) = 0;
    virtual void fill_rectangle(rectangle r) = 0;
    virtual void fill_poly(std::vector<point2d> const& points) = 0;
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
};

// Backward-compatible alias: all existing renderer* call sites continue to compile unchanged.
using renderer = irenderer;

} // namespace ezgl
