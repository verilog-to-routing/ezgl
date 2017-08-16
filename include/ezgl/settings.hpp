#ifndef EZGL_SETTINGS_HPP
#define EZGL_SETTINGS_HPP

#include <string>
#include <cairo.h>

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
 * Graphics backends supported by ezgl.
 */
enum class graphics_backend { cairo };

/**
 * The prototype of a function to be called on draw events.
 */
using draw_callback_fn = void (*)(cairo_t *cairo, int width, int height);

/**
 * Settings to configure how graphics are drawn.
 */
struct graphics_settings {
  /**
   * The graphics backend to use.
   */
  graphics_backend backend = graphics_backend::cairo;

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
