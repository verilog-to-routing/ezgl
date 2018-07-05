#ifndef EZGL_SETTINGS_HPP
#define EZGL_SETTINGS_HPP

#include <ezgl/graphics.hpp>

#include <gdk/gdk.h>

#include <string>

namespace ezgl {


/**
 * The prototype of a function to be called on draw events.
 *
 * The ezgl::graphics object provides drawing functionality.
 * The width and height provide the dimensions of the drawable area.
 */
using draw_callback_fn = void (*)(graphics g, int width, int height);

/**
 * Draw nothing on draw events.
 */
inline void draw_nothing(graphics, int, int)
{
}

/**
 * The prototype of a function to be called when a key is pressed.
 */
using key_press_callback_fn = void (*)(GdkEventKey *event);

/**
 * Ignore keyboard presses.
 */
inline void ignore(GdkEventKey *)
{
}

/**
 * The prototype of a function to be called when the mouse has moved.
 */
using mouse_move_callback_fn = void (*)(GdkEventMotion *event);

/**
 * Ignore mouse motion.
 */
inline void ignore(GdkEventMotion *)
{
}

/**
 * The prototype of a function to be called when the mouse has been clicked.
 */
using mouse_click_callback_fn = void (*)(GdkEventButton *event);

/**
 * Ignore mouse clicks.
 */
inline void ignore(GdkEventButton *)
{
}

}

#endif //EZGL_SETTINGS_HPP
