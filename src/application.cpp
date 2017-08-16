#include "ezgl/application.hpp"

namespace ezgl {

void application::activate(GtkApplication *app, gpointer user_data)
{
  auto ezgl_app = static_cast<ezgl::application *>(user_data);

  ezgl_app->m_window = gtk_application_window_new(app);

  gtk_window_set_title(GTK_WINDOW(ezgl_app->m_window), ezgl_app->m_settings.window.title.c_str());
  gtk_window_set_default_size(GTK_WINDOW(ezgl_app->m_window), ezgl_app->m_settings.window.width,
      ezgl_app->m_settings.window.height);

  gtk_widget_show_all(ezgl_app->m_window);
}

application::application(settings s)
    : m_settings(std::move(s))
    , m_application(gtk_application_new("com.github.mariobadr.ezgl.app", G_APPLICATION_FLAGS_NONE))
    , m_window(nullptr)
{
  g_signal_connect(m_application, "activate", G_CALLBACK(activate), this);
}

application::~application()
{
  g_object_unref(m_application);
}

int application::run(int argc, char **argv)
{
  // see:
  return g_application_run(G_APPLICATION(m_application), argc, argv);
}
}
