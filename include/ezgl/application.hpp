#ifndef EZGL_APPLICATION_HPP
#define EZGL_APPLICATION_HPP

#include <string>

#include <gtk/gtk.h>

/**
 * An easy to use library for creating a graphical user interface.
 */
namespace ezgl {

class application;

using setup_callbacks_fn = void (*)(application *app);

/**
 * Represents the core application.
 */
class application {
public:
  /**
   * Create an application.
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
   * Specficy the function that will connect GUI objects to user-defined callbacks.
   */
  void register_callbacks_with(setup_callbacks_fn function_pointer);

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
  GObject *get_object(gchar const *name) const;

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
  // The GTK application.
  GtkApplication *m_application;

  // The GUI builder that parses an XML user interface.
  GtkBuilder *m_builder;

  // The package path to the XML file that describes the UI.
  std::string m_main_ui;

  // The ID of the main window to add to our GTK application.
  std::string m_window_id;

  // The ID of the main canvas in our GTK application.
  std::string m_canvas_id;

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
