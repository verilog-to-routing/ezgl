#ifndef EZGL_APPLICATION_HPP
#define EZGL_APPLICATION_HPP

#include <ezgl/callbacks.hpp>

#include <string>

#include <gtk/gtk.h>

/**
 * An easy to use library for creating a graphical user interface.
 */
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
  application();

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
   * @param function_pointer The function that will draw to the main canvas.
   */
  void set_callback(draw_callback_fn function_pointer);

  /**
   * @param function_pointer The function that will handle keyboard presses.
   */
  void set_callback(key_press_callback_fn function_pointer);

  /**
   * @param function_pointer The function that will handle mouse movement.
   */
  void set_callback(mouse_move_callback_fn function_pointer);

  /**
   * @param function_pointer The function that will handle mouse clicks.
   */
  void set_callback(mouse_click_callback_fn function_pointer);

  /**
   * Retrieve a GLib Object.
   *
   * This is useful for retrieving GUI elements specified in your XML file(s).
   *
   * You should only call this function after the application has been run, otherwise the GUI elements will have not
   * been created yet.
   *
   * @param name The ID of the object.
   * @return The object with the ID, or NULL if it could not be found.
   */
  GObject *get_object(gchar const *name);

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
  // the GTK application
  GtkApplication *m_application;

  // the GUI builder
  GtkBuilder *m_builder;

  // each ezgl application contains a main window
  GtkWidget *m_window;

  // each ezgl application has a drawable canvas
  GtkWidget *m_canvas;

private:
  /**
   * A group of callbacks that handle different events.
   */
  struct callbacks {
    /**
   * Handler for draw events.
   *
   * Default handling is to draw nothing at all.
   */
    draw_callback_fn render = draw_nothing;

    /**
   * Handler for keyboard presses.
   *
   * Default handling is to ignore all keyboard presses.
   */
    key_press_callback_fn handle_key_press = ignore;

    /**
   * Handler for mouse movement.
   *
   * Default handling is to ignore all mouse movement.
   */
    mouse_move_callback_fn handle_mouse_move = ignore;

    /**
   * Handler for mouse clicks.
   *
   * Default handling is to ignore all mouse clicks.
   */
    mouse_click_callback_fn handle_mouse_click = ignore;
  };

  // a bundle of user-defined callbacks for different types of events
  callbacks m_callbacks;

private:
  // called when our GtkApplication is initialized for the first time
  static void startup(GtkApplication *gtk_app, gpointer user_data);

  // called when GTK activates our application for the first time
  static void activate(GtkApplication *gtk_app, gpointer user_data);

  // called when m_canvas needs to be redrawn
  static gboolean draw_canvas(GtkWidget *widget, cairo_t *cairo, gpointer data);

  // called when a key was pressed on the keyboard
  static gboolean press_key(GtkWidget *widget, GdkEventKey *event, gpointer data);

  // called when the mouse moved inside m_window
  static gboolean move_mouse(GtkWidget *widget, GdkEventMotion *event, gpointer data);

  // called when the mouse was clicked inside m_window
  static gboolean click_mouse(GtkWidget *widget, GdkEventButton *event, gpointer data);
};
}

#endif //EZGL_APPLICATION_HPP
