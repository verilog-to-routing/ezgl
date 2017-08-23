#ifndef EZGL_SETTINGS_HPP
#define EZGL_SETTINGS_HPP

#include <ezgl/graphics.hpp>

#include <gdk/gdk.h>

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

inline void draw_nothing(graphics, int, int)
{
}

/**
 * Settings to configure how graphics are drawn.
 */
struct graphics_settings {
  /**
   * The function to call on draw events.
   */
  draw_callback_fn draw_callback = draw_nothing;

  /**
   * The background colour of the window.
   */
  colour background = WHITE;
};

/**
 * The prototype of a function to be called when a key is pressed.
 */
using key_press_callback_fn = void (*)(GdkEventKey *event);

inline void no_reaction(GdkEventKey *)
{
}

/**
 * Settings to configure how to respond to input.
 */
struct input_settings {
  key_press_callback_fn key_press_callback = no_reaction;
};

/**
 * A bundle of all ezgl settings.
 */
struct settings {
  window_settings window;
  graphics_settings graphics;
  input_settings input;
};
}

#endif //EZGL_SETTINGS_HPP
