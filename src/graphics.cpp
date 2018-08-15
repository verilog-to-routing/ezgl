#include "ezgl/graphics.hpp"

#include "ezgl/camera.hpp"

#include <cassert>

namespace ezgl {

graphics::graphics(cairo_t *cairo, camera *cam) : m_cairo(cairo), m_camera(cam)
{
}

void graphics::set_colour(colour c)
{
  set_colour(c.red, c.green, c.blue, c.alpha);
}

void graphics::set_colour(colour c, uint_fast8_t alpha)
{
  set_colour(c.red, c.green, c.blue, alpha);
}

void graphics::set_colour(uint_fast8_t red,
    uint_fast8_t green,
    uint_fast8_t blue,
    uint_fast8_t alpha)
{
  cairo_set_source_rgba(m_cairo, red / 255.0, green / 255.0, blue / 255.0, alpha / 255.0);
}

void graphics::set_line_cap(line_cap cap)
{
  auto cairo_cap = static_cast<cairo_line_cap_t>(cap);
  cairo_set_line_cap(m_cairo, cairo_cap);
}

void graphics::set_line_dash(line_dash dash)
{
  if(dash == line_dash::none) {
    int num_dashes = 0; // disables dashing

    cairo_set_dash(m_cairo, nullptr, num_dashes, 0);
  } else if(dash == line_dash::asymmetric_5_3) {
    static double dashes[] = {5.0, 3.0};
    int num_dashes = 2; // asymmetric dashing

    cairo_set_dash(m_cairo, dashes, num_dashes, 0);
  }
}

void graphics::set_line_width(int width)
{
  cairo_set_line_width(m_cairo, width);
}

void graphics::set_font_size(double new_size)
{
  cairo_set_font_size(m_cairo, new_size);
}

void graphics::format_font(std::string const &family, font_slant slant, font_weight weight)
{
  cairo_select_font_face(m_cairo, family.c_str(), static_cast<cairo_font_slant_t>(slant),
      static_cast<cairo_font_weight_t>(weight));
}

void graphics::format_font(std::string const &family,
    font_slant slant,
    font_weight weight,
    double new_size)
{
  set_font_size(new_size);
  format_font(family, slant, weight);
}

void graphics::draw_line(point2d start, point2d end)
{
  start = m_camera->world_to_screen(start);
  end = m_camera->world_to_screen(end);

  cairo_move_to(m_cairo, start.x(), start.y());
  cairo_line_to(m_cairo, end.x(), end.y());

  cairo_stroke(m_cairo);
}

void graphics::draw_rectangle(point2d start, point2d end)
{
  draw_rectangle_path(start, end);
  cairo_stroke(m_cairo);
}

void graphics::draw_rectangle(point2d start, double width, double height)
{
  draw_rectangle_path(start, {start.x() + width, start.y() + height});
  cairo_stroke(m_cairo);
}

void graphics::draw_rectangle(rectangle r)
{
  draw_rectangle_path({r.left(), r.bottom()}, {r.right(), r.top()});
  cairo_stroke(m_cairo);
}

void graphics::fill_rectangle(point2d start, point2d end)
{
  draw_rectangle_path(start, end);
  cairo_fill(m_cairo);
}

void graphics::fill_rectangle(point2d start, double width, double height)
{
  draw_rectangle_path(start, {start.x() + width, start.y() + height});
  cairo_fill(m_cairo);
}

void graphics::fill_rectangle(rectangle r)
{
  draw_rectangle_path({r.left(), r.bottom()}, {r.right(), r.top()});
  cairo_fill(m_cairo);
}

void graphics::fill_poly(std::vector<point2d> const &points)
{
  assert(points.size() > 1);

  point2d next_point = m_camera->world_to_screen(points[0]);

  cairo_move_to(m_cairo, next_point.x(), next_point.y());

  for(std::size_t i = 1; i < points.size(); ++i) {
    next_point = m_camera->world_to_screen(points[i]);
    cairo_line_to(m_cairo, next_point.x(), next_point.y());
  }

  cairo_close_path(m_cairo);
  cairo_fill(m_cairo);
}

void graphics::draw_text(point2d centre, std::string const &text)
{
  cairo_text_extents_t text_extents{};
  cairo_text_extents(m_cairo, text.c_str(), &text_extents);

  cairo_font_extents_t font_extents{};
  cairo_font_extents(m_cairo, &font_extents);

  // see: https://www.cairographics.org/tutorial/#L1understandingtext
  centre = {centre.x() - text_extents.x_bearing - (text_extents.width / 2),
      centre.y() - font_extents.descent + (font_extents.height / 2)};

  centre = m_camera->world_to_screen(centre);

  cairo_move_to(m_cairo, centre.x(), centre.y());
  cairo_show_text(m_cairo, text.c_str());
}

void graphics::draw_rectangle_path(point2d start, point2d end)
{
  start = m_camera->world_to_screen(start);
  end = m_camera->world_to_screen(end);

  cairo_move_to(m_cairo, start.x(), start.y());
  cairo_line_to(m_cairo, start.x(), end.y());
  cairo_line_to(m_cairo, end.x(), end.y());
  cairo_line_to(m_cairo, end.x(), start.y());

  cairo_close_path(m_cairo);
}
}