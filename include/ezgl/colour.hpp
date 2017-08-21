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
 * The colour black.
 */
static constexpr auto black = colour{0, 0, 0};

/**
 * The colour blue.
 */
static constexpr auto blue = colour{0, 0, 255};

/**
 * The colour green.
 */
static constexpr auto green = colour{0, 255, 0};

/**
 * The colour red.
 */
static constexpr auto red = colour{255, 0, 0};

/**
 * The colour white.
 */
static constexpr auto white = colour{255, 255, 255};


}

#endif //EZGL_COLOUR_HPP
