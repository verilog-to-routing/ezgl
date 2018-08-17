#ifndef EZGL_CONTROL_HPP
#define EZGL_CONTROL_HPP

#include "point.hpp"

namespace ezgl {

class canvas;

void zoom_in(canvas *cnv, point2d zoom_point, double zoom_factor);

void zoom_out(canvas *cnv, point2d zoom_point, double zoom_factor);
}

#endif //EZGL_CONTROL_HPP
