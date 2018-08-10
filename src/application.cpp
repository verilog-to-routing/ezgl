#include "ezgl/application.hpp"

namespace ezgl {

void application::startup(GtkApplication *, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  char const *main_ui_resource = ezgl_app->m_main_ui.c_str();

  // Build the main user interface from the XML resource.
  GError *error = nullptr;
  if(gtk_builder_add_from_resource(ezgl_app->m_builder, main_ui_resource, &error) == 0) {
    g_error("%s.", error->message);
  }

  g_info("application::startup successful.");
}

void application::activate(GtkApplication *, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);

  // The main parent window needs to be explicitly added to our GTK application.
  GObject *window = ezgl_app->get_object(ezgl_app->m_window_id.c_str());
  gtk_application_add_window(ezgl_app->m_application, GTK_WINDOW(window));

  if(ezgl_app->m_register_callbacks != nullptr) {
    ezgl_app->m_register_callbacks(ezgl_app);
  } else {
    g_warning("No user-defined callbacks have been registered.");
  }

  // GtkDrawingArea objects need mouse button button presses enabled explicitly.
  GObject *canvas = ezgl_app->get_object(ezgl_app->m_canvas_id.c_str());
  gtk_widget_add_events(GTK_WIDGET(canvas), GDK_BUTTON_PRESS_MASK);

  g_info("application::activate successful.");
}

application::application(char const *main_ui_resource, char const *window_id, char const *canvas_id)
    : m_application(gtk_application_new("edu.toronto.eecg.ezgl.app", G_APPLICATION_FLAGS_NONE))
    , m_builder(gtk_builder_new())
    , m_main_ui(main_ui_resource)
    , m_window_id(window_id)
    , m_canvas_id(canvas_id)
    , m_register_callbacks(nullptr)
{
  // Connect our static functions application::{startup, activate} to their callbacks. We pass 'this' as the userdata
  // so that we can use it in our static functions.
  g_signal_connect(m_application, "startup", G_CALLBACK(startup), this);
  g_signal_connect(m_application, "activate", G_CALLBACK(activate), this);
}

application::~application()
{
  // GTK uses reference counting to track object lifetime. Since we called *_new() for our application and builder, we
  // need to unreference them. This should set their reference count to 0, letting GTK know that they should be cleaned
  // up in memory.
  g_object_unref(m_builder);
  g_object_unref(m_application);
}

void application::register_callbacks_with(setup_callbacks_fn fn)
{
  m_register_callbacks = fn;
}

GObject *application::get_object(gchar const *name) const
{
  // Getting an object from the GTK builder does not increase its reference count.
  GObject *object = gtk_builder_get_object(m_builder, name);
  if(object == nullptr) {
    g_error("Could not find a GUI object with the name %s.", name);
  }

  return object;
}

int application::run(int argc, char **argv)
{
  g_info("The event loop is now starting.");

  // see: https://developer.gnome.org/gio/unstable/GApplication.html#g-application-run
  return g_application_run(G_APPLICATION(m_application), argc, argv);
}
}
