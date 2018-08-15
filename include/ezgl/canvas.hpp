#ifndef EZGL_CANVAS_HPP
#define EZGL_CANVAS_HPP

#include <ezgl/camera.hpp>
#include <ezgl/rectangle.hpp>

#include <cairo.h>
#include <gtk/gtk.h>

#include <string>

namespace ezgl {

class graphics;

/**
 * The signature of a function that draws to the canvas.
 */
using draw_canvas_fn = void (*)(graphics &);

class canvas {
public:
  /**
   * Destructor.
   */
  ~canvas();

  /**
   * Get the name (identifier) of the canvas.
   */
  char const *id() const
  {
    return m_canvas_id.c_str();
  }

  /**
   * Get the width of the canvas in pixels.
   */
  int width() const;

  /**
   * Get the height of the canvas in pixels.
   */
  int height() const;

  void redraw();

protected:
  // Only the ezgl::application can create and initialize a canvas object.
  friend class application;

  /**
   * Create a canvas that can be drawn to.
   */
  canvas(std::string canvas_id, draw_canvas_fn draw_callback, rectangle coordinate_system);

  /**
   * Lazy initialization of the canvas class.
   *
   * This function is required because GTK will not send activate/startup signals to an ezgl::application until control
   * of the program has been reliquished. The GUI is not built until ezgl::application receives an activate signal.
   */
  void initialize(GtkWidget *drawing_area);

private:
  std::string m_canvas_id;

  draw_canvas_fn m_draw_callback;

  // A non-owning pointer to the drawing area inside a GTK window.
  GtkWidget *m_drawing_area = nullptr;

  // The off-screen surface that can be drawn to.
  cairo_surface_t *m_surface = nullptr;

  camera m_camera;

private:
  // Called each time our drawing area widget has changed (e.g., in size).
  static gboolean configure_event(GtkWidget *widget, GdkEventConfigure *event, gpointer data);

  // Called each time we need to draw to our drawing area widget.
  static gboolean draw_surface(GtkWidget *widget, cairo_t *context, gpointer data);
};
}

#endif //EZGL_CANVAS_HPP
