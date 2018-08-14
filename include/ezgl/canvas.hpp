#ifndef EZGL_CANVAS_HPP
#define EZGL_CANVAS_HPP

#include <ezgl/rectangle.hpp>

#include <cairo.h>
#include <gtk/gtk.h>

#include <string>

namespace ezgl {

/**
 * The signature of a function that draws to the canvas.
 */
using draw_canvas_fn = void (*)(cairo_t *);

class canvas {
public:
  /**
   * Create a canvas that can be drawn to.
   *
   * @param canvas_id The name of the main drawing area in the XML file.
   */
  canvas(std::string canvas_id, draw_canvas_fn draw_callback);

  void initialize(GtkWidget *drawing_area);

  void redraw();

  char const *id() const
  {
    return m_canvas_id.c_str();
  }

private:
  std::string m_canvas_id;

  draw_canvas_fn m_draw_callback;

  // A non-owning pointer to the drawing area inside a GTK window.
  GtkWidget *m_drawing_area = nullptr;

  // The off-screen surface that can be drawn to.
  cairo_surface_t *m_surface = nullptr;

private:
  // Called each time our drawing area widget has changed (e.g., in size).
  static gboolean configure_event(GtkWidget *widget, GdkEventConfigure *event, gpointer data);

  // Called each time we need to draw to our drawing area widget.
  static gboolean draw_surface(GtkWidget *widget, cairo_t *context, gpointer data);
};
}

#endif //EZGL_CANVAS_HPP
