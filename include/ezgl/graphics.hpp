#ifndef EZGL_GRAPHICS_HPP
#define EZGL_GRAPHICS_HPP

#include <ezgl/colour.hpp>
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
   * Draw text centred around a point.
   *
   * @param centre The centre of the text, in pixels.
   * @param text The text to draw.
   */
  void draw_text(point centre, std::string const &text);

private:
  cairo_t *m_cairo;
};
}

#endif //EZGL_GRAPHICS_HPP
