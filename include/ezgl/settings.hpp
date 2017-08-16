#ifndef EZGL_SETTINGS_HPP
#define EZGL_SETTINGS_HPP

#include <string>

namespace ezgl {

struct window_settings {
  std::string title = "My Window";
  int width = 640;
  int height = 480;
};

struct settings {
  window_settings window;
};

}

#endif //EZGL_SETTINGS_HPP
