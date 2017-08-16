#ifndef EZGL_SETTINGS_HPP
#define EZGL_SETTINGS_HPP

#include <ezgl/graphics.hpp>

#include <string>

namespace ezgl {

/**
 * Settings to configure the window.
 */
struct window_settings {
  /**
   * The title of the window shown at the top.
   */
  std::string title = "My Window";

  /**
   * The width of the window in pixels.
   */
  int width = 640;

  /**
   * The height of the window in pixels.
   */
  int height = 480;
};

/**
 * The prototype of a function to be called on draw events.
 *
 * The ezgl::graphics object provides drawing functionality.
 * The width and height provide the dimensions of the drawable area.
 */
using draw_callback_fn = void (*)(graphics g, int width, int height);

/**
 * Settings to configure how graphics are drawn.
 */
struct graphics_settings {
  /**
   * The function to call on draw events.
   */
  draw_callback_fn draw_callback = nullptr;
};

/**
 * A bundle of all ezgl settings.
 */
struct settings {
  window_settings window;
  graphics_settings graphics;
};
}

#endif //EZGL_SETTINGS_HPP
