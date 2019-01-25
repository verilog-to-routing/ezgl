#ifndef EZGL_CONTROL_HPP
#define EZGL_CONTROL_HPP

#include <ezgl/point.hpp>
#include <ezgl/rectangle.hpp>

namespace ezgl {

/**** Functions to manipulate what is visible on the screen; used by ezgl's predefined buttons. ****/
/**** Application code does not have to ever call these functions.                              ****/

class canvas;

/**
 * Zoom in on the center of the currently visible world.
 */
void zoom_in(canvas *cnv, double zoom_factor);

/**
 * Zoom out from the center of the currently visible world.
 */
void zoom_out(canvas *cnv, double zoom_factor);

/**
 * Zoom in on a specific point in the GTK widget.
 */
void zoom_in(canvas *cnv, point2d zoom_point, double zoom_factor);

/**
 * Zoom out from a specific point in GTK widget.
 */
void zoom_out(canvas *cnv, point2d zoom_point, double zoom_factor);

/**
 * Zoom in or out to fit an exact region of the world.
 */
void zoom_fit(canvas *cnv, rectangle region);

/**
 * Translate by delta x and delta y (dx, dy)
 */
void translate(canvas *cnv, double dx, double dy);

/**
 * Translate up
 */
void translate_up(canvas *cnv, double translate_factor);

/**
 * Translate down
 */
void translate_down(canvas *cnv, double translate_factor);

/**
 * Translate left
 */
void translate_left(canvas *cnv, double translate_factor);

/**
 * Translate right
 */
void translate_right(canvas *cnv, double translate_factor);
}

#endif //EZGL_CONTROL_HPP
