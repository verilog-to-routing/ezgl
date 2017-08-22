#ifndef EZGL_GRAPHICS_HPP
#define EZGL_GRAPHICS_HPP

#include <ezgl/colour.hpp>
#include <ezgl/font.hpp>
#include <ezgl/geometry.hpp>

#include <cairo.h>

#include <string>

namespace ezgl {

/**
 * A thin wrapper around a cairo graphics state.
 */
class graphics {
public:
  /**
   * Constructor.
   *
   * @param cairo The cairo graphics state.
   */
  explicit graphics(cairo_t *cairo);

  /**
   * Change the colour for subsequent draw calls.
   *
   * @param new_colour The new colour to use.
   * @param alpha The transparency level (0 is fully transparent, 1 is opaque).
   */
  void set_colour(colour new_colour);

  /**
   * Draw a line.
   *
   * @param start The start point of the line, in pixels.
   * @param end The end point of the line, in pixels.
   */
  void draw_line(point start, point end);

  /**
   * Draw the outline a rectangle.
   *
   * @param start The start point of the rectangle, in pixels.
   * @param end The end point of the rectangle, in pixels.
   */
  void draw_rectangle(point start, point end);

  /**
   * Draw the outline of a rectangle.
   *
   * @param start The start point of the rectangle, in pixels.
   * @param width How wide the rectangle is, in pixels.
   * @param height How high the rectangle is, in pixels.
   */
  void draw_rectangle(point start, double width, double height);

  /**
   * Draw a filled in rectangle.
   *
   * @param start The start point of the rectangle, in pixels.
   * @param end The end point of the rectangle, in pixels.
   */
  void fill_rectangle(point start, point end);

  /**
   * Draw a filled in rectangle.
   *
   * @param start The start point of the rectangle, in pixels.
   * @param width How wide the rectangle is, in pixels.
   * @param height How high the rectangle is, in pixels.
   */
  void fill_rectangle(point start, double width, double height);

  /**
   * Format the font with a new font face and size.
   *
   * @param new_format The new font face format.
   * @param new_size The new size.
   */
  void format_font(font_face const &new_format, double new_size);

  /**
   * Format the font with a new font face.
   *
   * @param new_format The new font face format.
   */
  void format_font_face(font_face const &new_format);

  /**
   * Change the size of the font.
   *
   * @param new_size The new size.
   */
  void format_font_size(double new_size);

  /**
   * Draw text centred around a point.
   *
   * @param centre The centre of the text, in pixels.
   * @param text The text to draw.
   */
  void draw_text(point centre, std::string const &text);

private:
  void draw_rectangle_path(point start, point end);

  cairo_t *m_cairo;
};
}

#endif //EZGL_GRAPHICS_HPP
