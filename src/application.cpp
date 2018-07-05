#include "ezgl/application.hpp"

namespace ezgl {

void application::startup(GtkApplication *, gpointer user_data)
{
  // "path" to the resource that contains an XML description of the UI
  static constexpr auto EZGL_MAIN_UI_RESOURCE = "/edu/toronto/eecg/ezgl/ece297/cd000/main.ui";

  auto ezgl_app = static_cast<application *>(user_data);

  // build the main user interface from the XML resource
  GError *error = nullptr;
  if(gtk_builder_add_from_resource(ezgl_app->m_builder, EZGL_MAIN_UI_RESOURCE, &error) == 0) {
    g_printerr("Error: %s\n", error->message);

    exit(EXIT_FAILURE);
  }

  // store pointers to the required GTK objects (MainWindow and MainCanvas)
  ezgl_app->m_window = GTK_WIDGET(gtk_builder_get_object(ezgl_app->m_builder, "MainWindow"));
  if(ezgl_app->m_window == nullptr) {
    g_printerr("Error: main.ui is missing a MainWindow.");

    exit(EXIT_FAILURE);
  }

  // enable the tracking of mouse movement in the MainWindow
  gtk_widget_add_events(ezgl_app->m_window, GDK_POINTER_MOTION_MASK);

  ezgl_app->m_canvas = GTK_WIDGET(gtk_builder_get_object(ezgl_app->m_builder, "MainCanvas"));
  if(ezgl_app->m_canvas == nullptr) {
    g_printerr("Error: main.ui is missing a MainCanvas.");

    exit(EXIT_FAILURE);
  }
}

void application::activate(GtkApplication *, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);

  // add the window to the application
  gtk_application_add_window(ezgl_app->m_application, GTK_WINDOW(ezgl_app->m_window));

  // connect to input events from the keyboard and mouse
  g_signal_connect(ezgl_app->m_window, "key_press_event", G_CALLBACK(press_key), user_data);
  g_signal_connect(ezgl_app->m_window, "motion_notify_event", G_CALLBACK(move_mouse), user_data);
  g_signal_connect(ezgl_app->m_window, "button_press_event", G_CALLBACK(click_mouse), user_data);
  g_signal_connect(ezgl_app->m_window, "button_release_event", G_CALLBACK(click_mouse), user_data);

  // connect to draw events for the canvas
  g_signal_connect(ezgl_app->m_canvas, "draw", G_CALLBACK(draw_canvas), user_data);
}

gboolean application::draw_canvas(GtkWidget *widget, cairo_t *cairo, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);

  graphics g(cairo);
  auto const width = gtk_widget_get_allocated_width(widget);
  auto const height = gtk_widget_get_allocated_height(widget);

  // do any additional drawing
  ezgl_app->m_callbacks.render(g, width, height);

  return FALSE; // propagate event
}

gboolean application::press_key(GtkWidget *, GdkEventKey *event, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  ezgl_app->m_callbacks.handle_key_press(event);

  return FALSE; // propagate event
}

gboolean application::move_mouse(GtkWidget *, GdkEventMotion *event, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  ezgl_app->m_callbacks.handle_mouse_move(event);

  return FALSE; // propagate event
}

gboolean application::click_mouse(GtkWidget *, GdkEventButton *event, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  ezgl_app->m_callbacks.handle_mouse_click(event);

  return FALSE; // propagate event
}

application::application()
    : m_application(gtk_application_new("edu.toronto.eecg.ezgl.app", G_APPLICATION_FLAGS_NONE))
    , m_builder(gtk_builder_new())
    , m_window(nullptr)
    , m_canvas(nullptr)
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

void application::set_callback(draw_callback_fn function_pointer)
{
  m_callbacks.render = function_pointer;
}

void application::set_callback(key_press_callback_fn function_pointer)
{
  m_callbacks.handle_key_press = function_pointer;
}

void application::set_callback(mouse_move_callback_fn function_pointer)
{
  m_callbacks.handle_mouse_move = function_pointer;
}

void application::set_callback(mouse_click_callback_fn function_pointer)
{
  m_callbacks.handle_mouse_click = function_pointer;
}

int application::run(int argc, char **argv)
{
  // see: https://developer.gnome.org/gio/unstable/GApplication.html#g-application-run
  return g_application_run(G_APPLICATION(m_application), argc, argv);
}
}
