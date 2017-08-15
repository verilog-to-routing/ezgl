#ifndef EZGL_APPLICATION_HPP
#define EZGL_APPLICATION_HPP

#include <string>

#include <gtk/gtk.h>

namespace ezgl {

class application;

application create_application(int argc, char **argv, std::string const &title);

class application {
public:
  int run();

private:
  friend application create_application(int, char **, std::string const &title);

  application(std::string const &title);

  GtkWidget *m_window;
};
}

#endif //EZGL_APPLICATION_HPP
