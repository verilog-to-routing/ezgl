#include "ezgl/application.hpp"

namespace ezgl {

void initialize_window(GtkApplication *gtk_app, GtkWidget *&window, window_settings const &settings)
{
  window = gtk_application_window_new(gtk_app);

  // setup the window
  gtk_window_set_title(GTK_WINDOW(window), settings.title.c_str());
  gtk_window_set_default_size(GTK_WINDOW(window), settings.width, settings.height);

  // enable the tracking of specific window events
  gtk_widget_add_events(window, GDK_POINTER_MOTION_MASK);
}

void initialize_canvas(GtkWidget *&window, GtkWidget *&canvas, graphics_settings const &settings)
{
  // create the drawing area
  canvas = gtk_drawing_area_new();
  gtk_widget_set_size_request(canvas, 600, 400); // TODO: fix width and height
  gtk_container_add(GTK_CONTAINER(window), canvas);
}

void application::activate(GtkApplication *gtk_app, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  auto const &settings = ezgl_app->m_settings;

  initialize_window(gtk_app, ezgl_app->m_window, settings.window);
  initialize_canvas(ezgl_app->m_window, ezgl_app->m_canvas, settings.graphics);

  // connect to input events from the keyboard and mouse
  g_signal_connect(ezgl_app->m_window, "key_press_event", G_CALLBACK(press_key), user_data);
  g_signal_connect(ezgl_app->m_window, "motion_notify_event", G_CALLBACK(move_mouse), user_data);
  // connect to draw events for the canvas
  g_signal_connect(ezgl_app->m_canvas, "draw", G_CALLBACK(draw_canvas), user_data);

  gtk_widget_show_all(ezgl_app->m_window);
}

gboolean application::draw_canvas(GtkWidget *widget, cairo_t *cairo, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  auto const &settings = ezgl_app->m_settings;

  auto const width = gtk_widget_get_allocated_width(widget);
  auto const height = gtk_widget_get_allocated_height(widget);

  graphics g(cairo);

  // draw the background with the configured colour
  g.set_colour(settings.graphics.background);
  cairo_paint(cairo);

  // do any additional drawing
  settings.graphics.draw_callback(g, width, height);

  return FALSE; // propagate event
}

gboolean application::press_key(GtkWidget *widget, GdkEventKey *event, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  auto const &settings = ezgl_app->m_settings;

  settings.input.key_press_callback(event);

  return FALSE; // propagate event
}

gboolean application::move_mouse(GtkWidget *widget, GdkEventMotion *event, gpointer user_data)
{
  auto ezgl_app = static_cast<application *>(user_data);
  auto const &settings = ezgl_app->m_settings;

  settings.input.mouse_move_callback(event);

  return FALSE; // propagate event
}

application::application(settings s)
    : m_settings(std::move(s))
    , m_application(gtk_application_new("com.github.mariobadr.ezgl.app", G_APPLICATION_FLAGS_NONE))
    , m_window(nullptr)
    , m_canvas(nullptr)
{
  g_signal_connect(m_application, "activate", G_CALLBACK(activate), this);
}

application::~application()
{
  g_object_unref(m_application);
}

int application::run(int argc, char **argv)
{
  // see: https://developer.gnome.org/gio/unstable/GApplication.html#g-application-run
  return g_application_run(G_APPLICATION(m_application), argc, argv);
}
}
