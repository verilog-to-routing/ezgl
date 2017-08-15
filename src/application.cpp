#include "ezgl/application.hpp"

namespace ezgl {

application create_application(int argc, char **argv)
{
  // initialize gtk - will abort application if it fails
  gtk_init(&argc, &argv);

  return application();
}

application::application() : m_window(gtk_window_new(GTK_WINDOW_TOPLEVEL))
{
}

int application::run()
{
  // show the application window
  gtk_widget_show_all(m_window);

  // enter the main event loop - GTK takes over from here
  gtk_main();

  return 0;
}
}
