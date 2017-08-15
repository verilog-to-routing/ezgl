#ifndef EZGL_APPLICATION_HPP
#define EZGL_APPLICATION_HPP

#include <gtk/gtk.h>

namespace ezgl {

class application;

application create_application(int argc, char **argv);

class application {
public:
  int run();
private:
  friend application create_application(int, char **);

  application();

  GtkWidget *m_window;
};
}

#endif //EZGL_APPLICATION_HPP
