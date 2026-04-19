#include "ezgl/qt/immediate_renderer.hpp"

#include <cassert>
#include <cfloat>
#include <utility>

namespace ezgl {

// ---- construction ----------------------------------------------------------

immediate_renderer::immediate_renderer(Painter* painter,
                                       transform_fn transform,
                                       camera* cam,
                                       QImage* surface)
    : irenderer(painter, std::move(transform), cam, surface)
{}

// ---- painter update --------------------------------------------------------

void immediate_renderer::update_renderer(Painter* painter, QImage* surface)
{
    update_painter(painter, surface);
}

// ---- draw calls ------------------------------------------------------------

void immediate_renderer::draw_line(const point2d& start, const point2d& end)
{
    paint_line(start, end);
}

void immediate_renderer::draw_rectangle(const point2d& start, const point2d& end)
{
    if (rectangle_off_screen({start, end}))
        return;
    paint_rectangle_path(start, end, false);
}

void immediate_renderer::draw_rectangle(const point2d& start, double width, double height)
{
    point2d end{start.x + width, start.y + height};
    if (rectangle_off_screen({start, end}))
        return;
    paint_rectangle_path(start, end, false);
}

void immediate_renderer::draw_rectangle(const rectangle& r)
{
    point2d bl{r.left(), r.bottom()};
    point2d tr{r.right(), r.top()};
    if (rectangle_off_screen({bl, tr}))
        return;
    paint_rectangle_path(bl, tr, false);
}

void immediate_renderer::fill_rectangle(const point2d& start, const point2d& end)
{
    if (rectangle_off_screen({start, end}))
        return;
    paint_rectangle_path(start, end, true);
}

void immediate_renderer::fill_rectangle(const point2d& start, double width, double height)
{
    point2d end{start.x + width, start.y + height};
    if (rectangle_off_screen({start, end}))
        return;
    paint_rectangle_path(start, end, true);
}

void immediate_renderer::fill_rectangle(const rectangle& r)
{
    point2d bl{r.left(), r.bottom()};
    point2d tr{r.right(), r.top()};
    if (rectangle_off_screen({bl, tr}))
        return;
    paint_rectangle_path(bl, tr, true);
}

void immediate_renderer::fill_triangle(const point2d& a, const point2d& b, const point2d& c)
{
    paint_poly({a, b, c});
}

void immediate_renderer::fill_poly(const std::vector<point2d>& points)
{
    assert(points.size() > 3 && "if points.size() == 3 use fill_triangle method instead, it's much faster");
    paint_poly(points);
}

void immediate_renderer::draw_elliptic_arc(const point2d& center, double radius_x,
                                           double radius_y, double start_angle,
                                           double extent_angle)
{
    if (rectangle_off_screen({{center.x - radius_x, center.y - radius_y},
                              {center.x + radius_x, center.y + radius_y}}))
        return;
    paint_arc_path(center, radius_x, start_angle, extent_angle,
                   radius_y / radius_x, false);
}

void immediate_renderer::draw_arc(const point2d& center, double radius,
                                  double start_angle, double extent_angle)
{
    if (rectangle_off_screen({{center.x - radius, center.y - radius},
                              {center.x + radius, center.y + radius}}))
        return;
    paint_arc_path(center, radius, start_angle, extent_angle, 1.0, false);
}

void immediate_renderer::fill_elliptic_arc(const point2d& center, double radius_x,
                                           double radius_y, double start_angle,
                                           double extent_angle)
{
    if (rectangle_off_screen({{center.x - radius_x, center.y - radius_y},
                              {center.x + radius_x, center.y + radius_y}}))
        return;
    paint_arc_path(center, radius_x, start_angle, extent_angle,
                   radius_y / radius_x, true);
}

void immediate_renderer::fill_arc(const point2d& center, double radius,
                                  double start_angle, double extent_angle)
{
    if (rectangle_off_screen({{center.x - radius, center.y - radius},
                              {center.x + radius, center.y + radius}}))
        return;
    paint_arc_path(center, radius, start_angle, extent_angle, 1.0, true);
}

void immediate_renderer::draw_text(const point2d& point, const std::string& text)
{
    paint_text(point, text, DBL_MAX, DBL_MAX);
}

void immediate_renderer::draw_text(const point2d& point, const std::string& text,
                                   double bound_x, double bound_y)
{
    paint_text(point, text, bound_x, bound_y);
}

void immediate_renderer::draw_surface(surface* p_surface, const point2d& anchor_point,
                                      double scale_factor)
{
    paint_surface(p_surface, anchor_point, scale_factor);
}

} // namespace ezgl
