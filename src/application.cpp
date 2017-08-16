#include "ezgl/application.hpp"

namespace ezgl {

application::application()
    : m_application(gtk_application_new("com.github.mariobadr.ezgl.app", G_APPLICATION_FLAGS_NONE))
{
}

int application::run(int argc, char **argv)
{
  g_application_run(G_APPLICATION(m_application), argc, argv);

  return 0;
}
}
