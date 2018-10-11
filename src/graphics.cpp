#include "ezgl/graphics.hpp"

#include <cassert>

namespace ezgl {

renderer::renderer(cairo_t *cairo, transform_fn transform) : m_cairo(cairo), m_transform(std::move(transform))
{
}

void renderer::set_colour(colour c)
{
  set_colour(c.red, c.green, c.blue, c.alpha);
}

void renderer::set_colour(colour c, uint_fast8_t alpha)
{
  set_colour(c.red, c.green, c.blue, alpha);
}

void renderer::set_colour(uint_fast8_t red,
    uint_fast8_t green,
    uint_fast8_t blue,
    uint_fast8_t alpha)
{
  cairo_set_source_rgba(m_cairo, red / 255.0, green / 255.0, blue / 255.0, alpha / 255.0);
}

void renderer::set_line_cap(line_cap cap)
{
  auto cairo_cap = static_cast<cairo_line_cap_t>(cap);
  cairo_set_line_cap(m_cairo, cairo_cap);
}

void renderer::set_line_dash(line_dash dash)
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

void renderer::set_line_width(int width)
{
  cairo_set_line_width(m_cairo, width);
}

void renderer::set_font_size(double new_size)
{
  cairo_set_font_size(m_cairo, new_size);
}

void renderer::format_font(std::string const &family, font_slant slant, font_weight weight)
{
  cairo_select_font_face(m_cairo, family.c_str(), static_cast<cairo_font_slant_t>(slant),
      static_cast<cairo_font_weight_t>(weight));
}

void renderer::format_font(std::string const &family,
    font_slant slant,
    font_weight weight,
    double new_size)
{
  set_font_size(new_size);
  format_font(family, slant, weight);
}

void renderer::draw_line(point2d start, point2d end)
{
  start = m_transform(start);
  end = m_transform(end);

  cairo_move_to(m_cairo, start.x, start.y);
  cairo_line_to(m_cairo, end.x, end.y);

  cairo_stroke(m_cairo);
}

void renderer::draw_rectangle(point2d start, point2d end)
{
  draw_rectangle_path(start, end);
  cairo_stroke(m_cairo);
}

void renderer::draw_rectangle(point2d start, double width, double height)
{
  draw_rectangle_path(start, {start.x + width, start.y + height});
  cairo_stroke(m_cairo);
}

void renderer::draw_rectangle(rectangle r)
{
  draw_rectangle_path({r.left(), r.bottom()}, {r.right(), r.top()});
  cairo_stroke(m_cairo);
}

void renderer::fill_rectangle(point2d start, point2d end)
{
  draw_rectangle_path(start, end);
  cairo_fill(m_cairo);
}

void renderer::fill_rectangle(point2d start, double width, double height)
{
  draw_rectangle_path(start, {start.x + width, start.y + height});
  cairo_fill(m_cairo);
}

void renderer::fill_rectangle(rectangle r)
{
  draw_rectangle_path({r.left(), r.bottom()}, {r.right(), r.top()});
  cairo_fill(m_cairo);
}

void renderer::fill_poly(std::vector<point2d> const &points)
{
  assert(points.size() > 1);

  point2d next_point = m_transform(points[0]);
  cairo_move_to(m_cairo, next_point.x, next_point.y);

  for(std::size_t i = 1; i < points.size(); ++i) {
    next_point = m_transform(points[i]);
    cairo_line_to(m_cairo, next_point.x, next_point.y);
  }

  cairo_close_path(m_cairo);
  cairo_fill(m_cairo);
}

void renderer::draw_elliptic_arc(point2d centre, double radius_x, double radius_y, double start_angle, double extent_angle)
{
  // define the stretch factor (i.e. An ellipse is a stretched circle)
  double stretch_factor = radius_y / radius_x;

  draw_arc_path(centre, radius_x, start_angle, extent_angle, stretch_factor, false);
	cairo_stroke(m_cairo);
}

void renderer::draw_arc(point2d centre, double radius, double start_angle, double extent_angle)
{
	draw_arc_path(centre, radius, start_angle, extent_angle, 1, false);
	cairo_stroke(m_cairo);
}

void renderer::fill_elliptic_arc(point2d centre, double radius_x, double radius_y, double start_angle, double extent_angle)
{
  // define the stretch factor (i.e. An ellipse is a stretched circle)
  double stretch_factor = radius_y / radius_x;

  draw_arc_path(centre, radius_x, start_angle, extent_angle, stretch_factor, true);
  cairo_fill(m_cairo);
}

void renderer::fill_arc(point2d centre, double radius, double start_angle, double extent_angle)
{
  draw_arc_path(centre, radius, start_angle, extent_angle, 1, true);
  cairo_fill(m_cairo);
}

void renderer::draw_text(point2d centre, std::string const &text)
{
  cairo_text_extents_t text_extents{};
  cairo_text_extents(m_cairo, text.c_str(), &text_extents);

  cairo_font_extents_t font_extents{};
  cairo_font_extents(m_cairo, &font_extents);

  // see: https://www.cairographics.org/tutorial/#L1understandingtext
  centre = {centre.x - text_extents.x_bearing - (text_extents.width / 2),
      centre.y - font_extents.descent + (font_extents.height / 2)};

  centre = m_transform(centre);

  cairo_move_to(m_cairo, centre.x, centre.y);
  cairo_show_text(m_cairo, text.c_str());
}

void renderer::draw_rectangle_path(point2d start, point2d end)
{
  start = m_transform(start);
  end = m_transform(end);

  cairo_move_to(m_cairo, start.x, start.y);
  cairo_line_to(m_cairo, start.x, end.y);
  cairo_line_to(m_cairo, end.x, end.y);
  cairo_line_to(m_cairo, end.x, start.y);

  cairo_close_path(m_cairo);
}

void renderer::draw_arc_path(point2d centre, double radius, double start_angle, double extent_angle, double stretch_factor, bool fill_flag)
{
  // save the current state to undo the scaling needed for drawing ellipse
  cairo_save(m_cairo);

  // point_x is a point on the arc outline
  point2d point_x = {centre.x + radius, centre.y};

  // transform the center point of the arc, and the other point
  centre = m_transform(centre);
  point_x = m_transform(point_x);

  // calculate the new radius after transforming to the new coordinates
  radius = point_x.x - centre.x;

  // scale the drawing by the stretch factor to draw elliptic circles
  cairo_scale(m_cairo, 1/stretch_factor, 1);
  centre.x = centre.x * stretch_factor;
  radius = radius * stretch_factor;

  // start a new path (forget the current point). Alternative for cairo_move_to() for drawing non-filled arc
  cairo_new_path(m_cairo);

  // if the arc will be filled in, start drawing from the center of the arc
  if (fill_flag)
    cairo_move_to(m_cairo, centre.x, centre.y);

  // calculating the ending angle
  double end_angle = start_angle + extent_angle;

  // draw the arc in counter clock-wise direction if the extent angle is positive
  if (extent_angle >= 0)
  {
	  cairo_arc_negative(m_cairo, centre.x, centre.y, radius, - start_angle * M_PI / 180, - end_angle * M_PI / 180);
  }
  // draw the arc in clock-wise direction if the extent angle is negative
  else
  {
	  cairo_arc(m_cairo, centre.x, centre.y, radius, - start_angle * M_PI / 180, - end_angle * M_PI / 180);
  }

  // if the arc will be filled in, return back to the center of the arc
  if (fill_flag)
     cairo_close_path(m_cairo);

  // restore the old state to undo the scaling needed for drawing ellipse
  cairo_restore(m_cairo);
}
}
