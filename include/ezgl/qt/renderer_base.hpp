#pragma once

// Internal header — not part of the public API.
// Provides shared state and immediate-mode QPainter implementations used by
// immediate_renderer, deferred_renderer, and rhi_renderer via protected inheritance.

#include "ezgl/irenderer.hpp"
#include "ezgl/camera.hpp"
#include "ezgl/qt/painter.hpp"
#include "ezgl/qt/qtutils.hpp"

#include <functional>
#include <string>
#include <vector>
#include <QFont>
#include <QImage>

namespace ezgl {

class RendererBase {
public:
    using transform_fn = std::function<point2d(point2d)>;

protected:
    RendererBase(Painter* painter, transform_fn transform, camera* cam, QImage* surface);

    // ---- State fields --------------------------------------------------------

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

    // ---- Painter / camera utilities ------------------------------------------

    // Update the active painter (e.g., after a surface resize). Restores style state.
    void update_painter(Painter* painter, QImage* surface);

    bool      rectangle_off_screen(rectangle rect);
    bool      clip_line_world(const rectangle& clip_window, point2d& start, point2d& end);
    rectangle get_visible_world_impl();
    rectangle get_visible_screen_impl() const;
    rectangle world_to_screen_impl(const rectangle& box);

    // ---- State-setter implementations ----------------------------------------
    // Each updates both the internal field and the active QPainter.

    void do_set_color(color c);
    void do_set_color(color c, uint_fast8_t alpha);
    void do_set_color(uint_fast8_t r, uint_fast8_t g, uint_fast8_t b, uint_fast8_t a = 255);
    void do_set_line_cap(line_cap cap);
    void do_set_line_dash(line_dash dash);
    void do_set_line_width(int width);
    void do_set_font_size(double size);
    void do_format_font(const std::string& family, font_slant slant, font_weight weight);
    void do_set_text_rotation(double degrees);
    void do_set_horiz_justification(justification j);
    void do_set_vert_justification(justification j);
    void do_set_coordinate_system(t_coordinate_system cs);
    void do_set_visible_world(rectangle new_world);

    // ---- Immediate-mode draw implementations ---------------------------------
    // Non-virtual; used directly by deferred_renderer::replay() and rhi_renderer
    // overlay replay so that virtual dispatch never re-enters the deferred path.

    void do_draw_line(const point2d& start, const point2d& end);
    void do_draw_rectangle_path(const point2d& start, const point2d& end, bool fill);
    void do_fill_poly(const std::vector<point2d>& points);
    void do_draw_arc_path(const point2d& center, double radius, double start_angle,
                          double extent_angle, double stretch_factor, bool fill);
    void do_draw_text(const point2d& point, const std::string& text,
                      double bound_x, double bound_y);
    void do_draw_surface(surface* p_surface, const point2d& anchor, double scale_factor);
};

} // namespace ezgl
