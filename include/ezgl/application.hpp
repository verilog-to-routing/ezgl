#ifndef EZGL_APPLICATION_HPP
#define EZGL_APPLICATION_HPP

#include <string>

#include <gtk/gtk.h>

namespace ezgl {

class application {
public:
  application();

  int run(int argc, char **argv);

private:
  GtkApplication *m_application;
};
}

#endif //EZGL_APPLICATION_HPP
