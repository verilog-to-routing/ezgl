#include "ezgl/application.hpp"

namespace ezgl {

void application::startup(GtkApplication *, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  char const *main_ui_resource = ezgl_app->m_main_ui.c_str();

  // Build the main user interface from the XML resource.
  GError *error = nullptr;
  if(gtk_builder_add_from_resource(ezgl_app->m_builder, main_ui_resource, &error) == 0) {
    g_printerr("Error: %s\n", error->message);

    exit(EXIT_FAILURE);
  }

  // Make sure that the window_id given exists in the GTK Builder.
  GObject *window = gtk_builder_get_object(ezgl_app->m_builder, ezgl_app->m_window_id.c_str());
  if(window == nullptr) {
    g_printerr("Error: main.ui is missing a window.");
    g_printerr("Error: The expected window ID was %s", ezgl_app->m_window_id.c_str());

    exit(EXIT_FAILURE);
  }
}

void application::activate(GtkApplication *, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);

  // Retrieve the window from the GTK Builder.
  GObject *window = gtk_builder_get_object(ezgl_app->m_builder, ezgl_app->m_window_id.c_str());
  // Add the window to our GTK Application.
  gtk_application_add_window(ezgl_app->m_application, GTK_WINDOW(window));

  if(ezgl_app->m_register_callbacks != nullptr) {
    ezgl_app->m_register_callbacks(ezgl_app);
  }

  // Retrieve the drawing area from the GTK Builder.
  GObject *canvas = gtk_builder_get_object(ezgl_app->m_builder, ezgl_app->m_canvas_id.c_str());
  // Enable mouse events for the drawing area.
  gtk_widget_add_events(GTK_WIDGET(canvas), GDK_BUTTON_PRESS_MASK);
}

application::application(char const *main_ui_resource, char const *window_id, char const *canvas_id)
    : m_application(gtk_application_new("edu.toronto.eecg.ezgl.app", G_APPLICATION_FLAGS_NONE))
    , m_builder(gtk_builder_new())
    , m_main_ui(main_ui_resource)
    , m_window_id(window_id)
    , m_canvas_id(canvas_id)
    , m_register_callbacks(nullptr)
{
  // connect our static functions application::{startup, activate} to their callbacks
  // we also pass 'this' object as the userdata so that we can use it in our static function
  g_signal_connect(m_application, "startup", G_CALLBACK(startup), this);
  g_signal_connect(m_application, "activate", G_CALLBACK(activate), this);
}

application::~application()
{
  g_object_unref(m_application);
}

void application::register_callbacks_with(setup_callbacks_fn function_pointer)
{
  m_register_callbacks = function_pointer;
}

GObject *application::get_object(gchar const *name) const
{
  return gtk_builder_get_object(m_builder, name);
}

int application::run(int argc, char **argv)
{
  // see: https://developer.gnome.org/gio/unstable/GApplication.html#g-application-run
  return g_application_run(G_APPLICATION(m_application), argc, argv);
}
}
