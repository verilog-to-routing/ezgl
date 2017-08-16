#include "ezgl/application.hpp"

namespace ezgl {

void application::activate(GtkApplication *app, gpointer user_data)
{
  auto ezgl_app = static_cast<ezgl::application *>(user_data);

  ezgl_app->m_window = gtk_application_window_new(app);
  gtk_widget_show_all(ezgl_app->m_window);
}

application::application()
    : m_application(gtk_application_new("com.github.mariobadr.ezgl.app", G_APPLICATION_FLAGS_NONE))
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
  return g_application_run(G_APPLICATION(m_application), argc, argv);
}
}
