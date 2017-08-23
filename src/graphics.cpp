#include "ezgl/graphics.hpp"

namespace ezgl {

graphics::graphics(cairo_t *cairo) : m_cairo(cairo)
{
}

void graphics::set_colour(colour c)
{
  set_colour(c.red, c.green, c.blue, 1);
}

void graphics::set_colour(colour c, double alpha)
{
  set_colour(c.red, c.green, c.blue, alpha);
}

void graphics::set_colour(double red, double green, double blue, double alpha)
{
  cairo_set_source_rgba(m_cairo, red, green, blue, alpha);
}

void graphics::draw_line(point start, point end)
{
  cairo_move_to(m_cairo, start.x, start.y);
  cairo_line_to(m_cairo, end.x, end.y);

  cairo_stroke(m_cairo);
}

void graphics::draw_rectangle(point start, point end)
{
  draw_rectangle_path(start, end);

  cairo_stroke(m_cairo);
}

void graphics::draw_rectangle(point start, double width, double height)
{
  draw_rectangle_path(start, {start.x + width, start.y + height});

  cairo_stroke(m_cairo);
}

void graphics::fill_rectangle(point start, point end)
{
  draw_rectangle_path(start, end);

  cairo_fill(m_cairo);
}

void graphics::fill_rectangle(point start, double width, double height)
{
  draw_rectangle_path(start, {start.x + width, start.y + height});

  cairo_fill(m_cairo);
}

void graphics::draw_text(point centre, std::string const &text)
{
  cairo_text_extents_t text_extents{};
  cairo_text_extents(m_cairo, text.c_str(), &text_extents);

  cairo_font_extents_t font_extents{};
  cairo_font_extents(m_cairo, &font_extents);

  // see: https://www.cairographics.org/tutorial/#L1understandingtext
  centre.x = centre.x - text_extents.x_bearing - (text_extents.width / 2);
  centre.y = centre.y - font_extents.descent + (font_extents.height / 2);

  cairo_move_to(m_cairo, centre.x, centre.y);
  cairo_show_text(m_cairo, text.c_str());
}

void graphics::format_font(double new_size)
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
  format_font(new_size);
  format_font(family, slant, weight);
}

void graphics::draw_rectangle_path(point start, point end)
{
  cairo_move_to(m_cairo, start.x, start.y);
  cairo_line_to(m_cairo, start.x, end.y);
  cairo_line_to(m_cairo, end.x, end.y);
  cairo_line_to(m_cairo, end.x, start.y);

  cairo_close_path(m_cairo);
}
}