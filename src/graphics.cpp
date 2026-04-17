/*
 * Copyright 2019-2022 University of Toronto
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Authors: Mario Badr, Sameh Attia, Tanner Young-Schultz and Vaughn Betz
 */

#include "ezgl/graphics.hpp"

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>

namespace ezgl {

// ---- Construction -----------------------------------------------------------

immediate_renderer::immediate_renderer(Painter* painter,
                                       transform_fn transform,
                                       camera* cam,
                                       QImage* surface)
    : RendererBase(painter, std::move(transform), cam, surface)
{}

// ---- Coordinate system / viewport ------------------------------------------

void immediate_renderer::set_coordinate_system(t_coordinate_system cs)
{
    do_set_coordinate_system(cs);
}

void immediate_renderer::set_visible_world(rectangle new_world)
{
    do_set_visible_world(new_world);
}

rectangle immediate_renderer::get_visible_world()
{
    return get_visible_world_impl();
}

rectangle immediate_renderer::get_visible_screen() const
{
    return get_visible_screen_impl();
}

rectangle immediate_renderer::world_to_screen(const rectangle& box)
{
    return world_to_screen_impl(box);
}

// ---- State setters ----------------------------------------------------------

void immediate_renderer::set_color(color c)
{
    do_set_color(c);
}

void immediate_renderer::set_color(color c, uint_fast8_t alpha)
{
    do_set_color(c, alpha);
}

void immediate_renderer::set_color(uint_fast8_t r, uint_fast8_t g,
                                   uint_fast8_t b, uint_fast8_t a)
{
    do_set_color(r, g, b, a);
}

void immediate_renderer::set_line_cap(line_cap cap)
{
    do_set_line_cap(cap);
}

void immediate_renderer::set_line_dash(line_dash dash)
{
    do_set_line_dash(dash);
}

void immediate_renderer::set_line_width(int width)
{
    do_set_line_width(width);
}

void immediate_renderer::set_font_size(double new_size)
{
    do_set_font_size(new_size);
}

void immediate_renderer::format_font(std::string const& family,
                                     font_slant slant, font_weight weight)
{
    do_format_font(family, slant, weight);
}

void immediate_renderer::format_font(std::string const& family,
                                     font_slant slant, font_weight weight,
                                     double new_size)
{
    do_set_font_size(new_size);
    do_format_font(family, slant, weight);
}

void immediate_renderer::set_text_rotation(double degrees)
{
    do_set_text_rotation(degrees);
}

void immediate_renderer::set_horiz_justification(justification horiz_just)
{
    do_set_horiz_justification(horiz_just);
}

void immediate_renderer::set_vert_justification(justification vert_just)
{
    do_set_vert_justification(vert_just);
}

// ---- Draw calls -------------------------------------------------------------

void immediate_renderer::draw_line(const point2d& start, const point2d& end)
{
    if (rectangle_off_screen({start, end}))
        return;
    do_draw_line(start, end);
}

void immediate_renderer::draw_rectangle(const point2d& start, const point2d& end)
{
    if (rectangle_off_screen({start, end}))
        return;
    do_draw_rectangle_path(start, end, false);
}

void immediate_renderer::draw_rectangle(const point2d& start, double width, double height)
{
    point2d end{start.x + width, start.y + height};
    if (rectangle_off_screen({start, end}))
        return;
    do_draw_rectangle_path(start, end, false);
}

void immediate_renderer::draw_rectangle(rectangle r)
{
    point2d bl{r.left(), r.bottom()};
    point2d tr{r.right(), r.top()};
    if (rectangle_off_screen({bl, tr}))
        return;
    do_draw_rectangle_path(bl, tr, false);
}

void immediate_renderer::fill_rectangle(const point2d& start, const point2d& end)
{
    if (rectangle_off_screen({start, end}))
        return;
    do_draw_rectangle_path(start, end, true);
}

void immediate_renderer::fill_rectangle(const point2d& start, double width, double height)
{
    point2d end{start.x + width, start.y + height};
    if (rectangle_off_screen({start, end}))
        return;
    do_draw_rectangle_path(start, end, true);
}

void immediate_renderer::fill_rectangle(rectangle r)
{
    point2d bl{r.left(), r.bottom()};
    point2d tr{r.right(), r.top()};
    if (rectangle_off_screen({bl, tr}))
        return;
    do_draw_rectangle_path(bl, tr, true);
}

void immediate_renderer::fill_poly(std::vector<point2d> const& points)
{
    assert(points.size() > 1);

    double x_min = points[0].x, x_max = points[0].x;
    double y_min = points[0].y, y_max = points[0].y;
    for (std::size_t i = 1; i < points.size(); ++i) {
        x_min = std::min(x_min, points[i].x);
        x_max = std::max(x_max, points[i].x);
        y_min = std::min(y_min, points[i].y);
        y_max = std::max(y_max, points[i].y);
    }
    if (rectangle_off_screen({{x_min, y_min}, {x_max, y_max}}))
        return;

    do_fill_poly(points);
}

void immediate_renderer::draw_elliptic_arc(const point2d& center, double radius_x,
                                           double radius_y, double start_angle,
                                           double extent_angle)
{
    if (rectangle_off_screen({{center.x - radius_x, center.y - radius_y},
                               {center.x + radius_x, center.y + radius_y}}))
        return;
    do_draw_arc_path(center, radius_x, start_angle, extent_angle,
                     radius_y / radius_x, false);
}

void immediate_renderer::draw_arc(const point2d& center, double radius,
                                  double start_angle, double extent_angle)
{
    if (rectangle_off_screen({{center.x - radius, center.y - radius},
                               {center.x + radius, center.y + radius}}))
        return;
    do_draw_arc_path(center, radius, start_angle, extent_angle, 1.0, false);
}

void immediate_renderer::fill_elliptic_arc(const point2d& center, double radius_x,
                                           double radius_y, double start_angle,
                                           double extent_angle)
{
    if (rectangle_off_screen({{center.x - radius_x, center.y - radius_y},
                               {center.x + radius_x, center.y + radius_y}}))
        return;
    do_draw_arc_path(center, radius_x, start_angle, extent_angle,
                     radius_y / radius_x, true);
}

void immediate_renderer::fill_arc(const point2d& center, double radius,
                                  double start_angle, double extent_angle)
{
    if (rectangle_off_screen({{center.x - radius, center.y - radius},
                               {center.x + radius, center.y + radius}}))
        return;
    do_draw_arc_path(center, radius, start_angle, extent_angle, 1.0, true);
}

void immediate_renderer::draw_text(const point2d& point, std::string const& text)
{
    do_draw_text(point, text, DBL_MAX, DBL_MAX);
}

void immediate_renderer::draw_text(const point2d& point, std::string const& text,
                                   double bound_x, double bound_y)
{
    do_draw_text(point, text, bound_x, bound_y);
}

void immediate_renderer::draw_surface(surface* p_surface, const point2d& anchor_point,
                                      double scale_factor)
{
    do_draw_surface(p_surface, anchor_point, scale_factor);
}

// ---- Painter update ---------------------------------------------------------

void immediate_renderer::update_renderer(Painter* painter, QImage* surface)
{
    update_painter(painter, surface);
}

} // namespace ezgl
