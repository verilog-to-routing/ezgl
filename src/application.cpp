#include "ezgl/application.hpp"

namespace ezgl {

void application::activate(GtkApplication *app, gpointer user_data)
{
  auto ezgl_app = static_cast<ezgl::application *>(user_data);

  ezgl_app->m_window = gtk_application_window_new(app);

  gtk_window_set_title(GTK_WINDOW(ezgl_app->m_window), ezgl_app->m_settings.window.title.c_str());
  gtk_window_set_default_size(GTK_WINDOW(ezgl_app->m_window), ezgl_app->m_settings.window.width,
      ezgl_app->m_settings.window.height);

  ezgl_app->m_canvas = gtk_drawing_area_new();
  gtk_widget_set_size_request(ezgl_app->m_canvas, 600, 400);
  gtk_container_add(GTK_CONTAINER(ezgl_app->m_window), ezgl_app->m_canvas);
  g_signal_connect(G_OBJECT(ezgl_app->m_canvas), "draw", G_CALLBACK(draw_canvas), user_data);

  gtk_widget_show_all(ezgl_app->m_window);
}

gboolean application::draw_canvas(GtkWidget *widget, cairo_t *cairo, gpointer user_data)
{
  auto ezgl_app = static_cast<ezgl::application *>(user_data);

  auto context = gtk_widget_get_style_context(widget);
  auto const width = gtk_widget_get_allocated_width(widget);
  auto const height = gtk_widget_get_allocated_height(widget);

  gtk_render_background(context, cairo, 0, 0, width, height);

  if(ezgl_app->m_settings.graphics.draw_callback != nullptr) {
    ezgl_app->m_settings.graphics.draw_callback(graphics{cairo}, width, height);
  }

  cairo_fill(cairo);

  return FALSE; // propogate the event further
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
