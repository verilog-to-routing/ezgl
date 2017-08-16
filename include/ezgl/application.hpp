#ifndef EZGL_APPLICATION_HPP
#define EZGL_APPLICATION_HPP

#include <string>

#include <gtk/gtk.h>

namespace ezgl {

class application {
public:
  application();

  ~application();

  int run(int argc, char **argv);

private:
  static void activate(GtkApplication *app, gpointer user_data);

  GtkApplication *m_application;

  GtkWidget *m_window;
};
}

#endif //EZGL_APPLICATION_HPP
