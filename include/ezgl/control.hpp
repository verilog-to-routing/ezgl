#ifndef EZGL_CONTROL_HPP
#define EZGL_CONTROL_HPP

#include <ezgl/point.hpp>
#include <ezgl/rectangle.hpp>

namespace ezgl {

class canvas;

/**
 * Zoom in on the centre of the currently visible world.
 */
void zoom_in(canvas *cnv, double zoom_factor);

/**
 * Zoom out from the centre of the currently visible world.
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
}

#endif //EZGL_CONTROL_HPP
