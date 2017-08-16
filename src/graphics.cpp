#include "ezgl/graphics.hpp"

namespace ezgl {

graphics::graphics(cairo_t *cairo) : m_cairo(cairo)
{
}

void graphics::set_colour(colour c, double alpha)
{
  cairo_set_source_rgba(m_cairo, c.red, c.green, c.blue, alpha);
}

void graphics::draw_line(point start, point end)
{
  cairo_move_to(m_cairo, start.x, start.y);
  cairo_line_to(m_cairo, end.x, end.y);

  cairo_stroke(m_cairo);
}

void graphics::draw_rectangle(point start, point end)
{
  cairo_move_to(m_cairo, start.x, start.y);
  cairo_line_to(m_cairo, start.x, end.y);
  cairo_line_to(m_cairo, end.x, end.y);
  cairo_line_to(m_cairo, end.x, start.y);

  cairo_close_path(m_cairo);
  cairo_stroke(m_cairo);
}

void graphics::draw_rectangle(point start, double width, double height)
{
  cairo_move_to(m_cairo, start.x, start.y);
  cairo_line_to(m_cairo, start.x, start.y + height);
  cairo_line_to(m_cairo, start.x + width, start.y + height);
  cairo_line_to(m_cairo, start.x + width, start.y);

  cairo_close_path(m_cairo);
  cairo_stroke(m_cairo);
}

void graphics::fill_rectangle(point start, point end)
{
  cairo_move_to(m_cairo, start.x, start.y);
  cairo_line_to(m_cairo, start.x, end.y);
  cairo_line_to(m_cairo, end.x, end.y);
  cairo_line_to(m_cairo, end.x, start.y);

  cairo_close_path(m_cairo);
  cairo_fill(m_cairo);
}

void graphics::fill_rectangle(point start, double width, double height)
{
  cairo_move_to(m_cairo, start.x, start.y);
  cairo_line_to(m_cairo, start.x, start.y + height);
  cairo_line_to(m_cairo, start.x + width, start.y + height);
  cairo_line_to(m_cairo, start.x + width, start.y);

  cairo_close_path(m_cairo);
  cairo_fill(m_cairo);
}
}