#ifndef EZGL_APPLICATION_HPP
#define EZGL_APPLICATION_HPP

#include <ezgl/settings.hpp>

#include <string>

#include <gtk/gtk.h>

namespace ezgl {

/**
 * Represents the core application.
 */
class application {
public:
  /**
   * Create an application.
   *
   * @param s The settings to use for this application.
   */
  explicit application(settings s);

  /**
   * Destructor.
   */
  ~application();

  /**
   * Copies are disabled - there should be only one application object.
   */
  application(application const &) = delete;

  /**
   * Copies are disabled - there should be only one application object.
   */
  application &operator=(application const &) = delete;

  /**
   * Ownership of an application is transferrable.
   */
  application(application &&) = default;

  /**
   * Ownership of an application is transferrable.
   */
  application &operator=(application &&) = default;

  /**
   * Run the application.
   *
   * @param argc The number of arguments.
   * @param argv An array of the arguments.
   *
   * @return The exit status.
   */
  int run(int argc, char **argv);

private:
  static void activate(GtkApplication *app, gpointer user_data);

  static gboolean draw_canvas(GtkWidget *widget, cairo_t *cr, gpointer data);

  settings m_settings;

  GtkApplication *m_application;

  GtkWidget *m_window;

  GtkWidget *m_canvas;
};
}

#endif //EZGL_APPLICATION_HPP
