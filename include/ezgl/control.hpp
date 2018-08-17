#ifndef EZGL_CONTROL_HPP
#define EZGL_CONTROL_HPP

#include <ezgl/point.hpp>
#include <ezgl/rectangle.hpp>

namespace ezgl {

class canvas;

void zoom_in(canvas *cnv, point2d zoom_point, double zoom_factor);

void zoom_out(canvas *cnv, point2d zoom_point, double zoom_factor);

void zoom_fit(canvas *cnv, rectangle new_world);
}

#endif //EZGL_CONTROL_HPP
