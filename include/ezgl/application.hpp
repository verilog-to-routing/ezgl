#ifndef EZGL_APPLICATION_HPP
#define EZGL_APPLICATION_HPP

#include <ezgl/canvas.hpp>

#include <map>
#include <memory>
#include <string>

#include <gtk/gtk.h>

/**
 * A library for creating a graphical user interface.
 */
namespace ezgl {

class application;

/**
 * The signature of a function that connects GObject to functions via signals.
 *
 * @see application::get_object.
 */
using connect_g_objects_fn = void (*)(application *app);

/**
 * The core application.
 *
 * The GUI of an application is created from an XML file. Widgets created in the XML file can be retrieved from an
 * application object, but only after the object has been initialized by GTK via application::run.
 */
class application {
public:
  /**
   * Configuration settings for the applicaton.
   *
   * The GUI will be built from the XML description given by main_ui_resource.
   * The XML file must contain a GtkWindow with the name in window_identifier.
   */
  struct settings {
    /**
     * The resource path that contains the XML file, which describes the GUI.
     */
    std::string main_ui_resource;

    /**
     * The name of the main window in the XML file.
     */
    std::string window_identifier;

    /**
     * Specficy the function that will connect GUI objects to user-defined callbacks.
     *
     * GUI objects (i.e., a GObject) can be retrieved from this application object. These objects can then be connected
     * to specific events using g_signal_connect. A list of signals that can be used to make these connections can be
     * found <a href = "https://developer.gnome.org/gtk3/stable/GtkWidget.html#GtkWidget.signals">here</a>.
     *
     * @see application::get_object
     */
    connect_g_objects_fn setup_callbacks = nullptr;
  };

public:
  /**
   * Create an application.
   *
   * @param s The preconfigured settings.
   */
  explicit application(application::settings s);

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
   * Retrieve a pointer to a canvas that was previously added to the application.
   *
   * Calling this function before application::run results in undefined behaviour.
   *
   * @param canvas_id The key used when the canvas was added.
   *
   * @return A non-owning pointer, or nullptr if not found.
   *
   * @see application::get_object
   */
  canvas *get_canvas(std::string const &canvas_id) const;

  /**
   * Add a canvas to the application.
   *
   * If the canvas has already been added, it will not be overwritten and a warning will be displayed.
   *
   * @param canvas_id The id of the GtkDrawingArea in the XML file.
   * @param draw_callback The function to call that draws to this canvas.
   * @param coordinate_system The initial coordinate system of this canvas.
   *
   * @return A pointer to the newly created cavas.
   */
  canvas *add_canvas(std::string const &canvas_id, draw_canvas_fn draw_callback, rectangle coordinate_system);

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

  // The GTK application.
  GtkApplication *m_application;

  // The GUI builder that parses an XML user interface.
  GtkBuilder *m_builder;

  // The function to call when the application is starting up.
  connect_g_objects_fn m_register_callbacks;

  // The collection of canvases added to the application.
  std::map<std::string, std::unique_ptr<canvas>> m_canvases;

private:
  // Called when our GtkApplication is initialized for the first time.
  static void startup(GtkApplication *gtk_app, gpointer user_data);

  // Called when GTK activates our application for the first time.
  static void activate(GtkApplication *gtk_app, gpointer user_data);
};
}

#endif //EZGL_APPLICATION_HPP
