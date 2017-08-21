#ifndef EZGL_COLOUR_HPP
#define EZGL_COLOUR_HPP

namespace ezgl {

/**
 * Represents a colour as a mixture or red, green, and blue.
 */
struct colour {
  double red;
  double green;
  double blue;
};

/**
 * The colour red.
 */
static constexpr auto red = colour{255, 0, 0};

/**
 * The colour green.
 */
static constexpr auto green = colour{0, 255, 0};

/**
 * The colour blue.
 */
static constexpr auto blue = colour{0, 0, 255};
}

#endif //EZGL_COLOUR_HPP
