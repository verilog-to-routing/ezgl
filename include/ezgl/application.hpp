#ifndef EZGL_APPLICATION_HPP
#define EZGL_APPLICATION_HPP

#include <ezgl/canvas.hpp>

#include <string>

#include <gtk/gtk.h>

/**
 * A library for creating a graphical user interface.
 */
namespace ezgl {

class application;

/**
 * The signature of a function that registers callbacks with an application.
 *
 * @see application::register_callbacks_with, application::get_object.
 */
using setup_callbacks_fn = void (*)(application *app);

/**
 * Represents the core application.
 *
 * EZGL applications consist of, at minimum, a main window (GtkWindow) and a main canvas (GtkDrawingArea).
 */
class application {
public:
  /**
   * Create an application.
   *
   * The GUI will be built from the XML description given. This XML file must contain a GtkWindow with the name
   * window_id and a GtkDrawingArea with the name canvas_id.
   *
   * @param main_ui_resource The resource that describes the GUI in XML.
   * @param window_id The name of the main window the in XML file.
   * @param canvas_id The name of the main drawing area in the XML file.
   */
  application(char const *main_ui_resource, char const *window_id, char const *canvas_id);

  /**
   * Destructor.
   */
  ~application();

  /**
   * Copies are disabled.
   */
  application(application const &) = delete;

  /**
   * Copies are disabled.
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
   * Specficy the function that will connect GUI objects to user-defined callbacks.
   *
   * GUI objects (i.e., a GObject) can be retrieved from this application object. These objects can then be connected
   * to specific events using g_signal_connect. A list of signals that can be used to make these connections can be
   * found <a href = "https://developer.gnome.org/gtk3/stable/GtkWidget.html#GtkWidget.signals">here</a>.
   *
   * @param fn A pointer to a function that will be called once during initialization.
   *
   * @see application::get_object
   */
  void register_callbacks_with(setup_callbacks_fn fn);

  /**
   * Retrieve a GLib Object (i.e., a GObject).
   *
   * This is useful for retrieving GUI elements specified in your XML file(s). You should only call this function after
   * the application has been run, otherwise the GUI elements will have not been created yet.
   *
   * @param name The ID of the object.
   * @return The object with the ID, or NULL if it could not be found.
   *
   * @see application::run
   */
  GObject *get_object(gchar const *name) const;

  /**
   * Run the application.
   *
   * Once this is called, the application will be initialized first. Initialization will build the GUI based on the XML
   * resource given in the constructor. Once the GUI has been created, the function set in
   * application::register_callbacks_with will be called.
   *
   * After initialization, control of the program will be given to GTK. You will only regain control for the signals
   * that you have registered callbacks for.
   *
   * @param argc The number of arguments.
   * @param argv An array of the arguments.
   *
   * @return The exit status.
   */
  int run(int argc, char **argv);

private:
  // The package path to the XML file that describes the UI.
  std::string m_main_ui;

  // The ID of the main window to add to our GTK application.
  std::string m_window_id;

  canvas m_canvas;

  // The GTK application.
  GtkApplication *m_application;

  // The GUI builder that parses an XML user interface.
  GtkBuilder *m_builder;

  // The function to call when the application is starting up.
  setup_callbacks_fn m_register_callbacks;

private:
  // Called when our GtkApplication is initialized for the first time.
  static void startup(GtkApplication *gtk_app, gpointer user_data);

  // Called when GTK activates our application for the first time.
  static void activate(GtkApplication *gtk_app, gpointer user_data);
};
}

#endif //EZGL_APPLICATION_HPP
