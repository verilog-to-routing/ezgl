#ifndef EZGL_COLOUR_HPP
#define EZGL_COLOUR_HPP

namespace ezgl {

/**
 * Represents a colour as a mixture or red, green, and blue as well as the colour's transparency.
 */
struct colour {
  /**
   * A red component of the colour, between 0.0 and 1.0.
   */
  double red;

  /**
   * The green component of the colour, between 0.0 and 1.0.
   */
  double green;

  /**
   * The blue component of the colour, between 0.0 and 1.0.
   */
  double blue;

  /**
   * The level of transparency, where 0.0 is fully transparent and 1.0 is opaque.
   */
  double alpha;
};

/**
 * The colour black.
 */
static constexpr auto BLACK = colour{0, 0, 0, 1};

/**
 * The colour blue.
 */
static constexpr auto BLUE = colour{0, 0, 1, 1};

/**
 * The colour green.
 */
static constexpr auto GREEN = colour{0, 1, 0, 1};

/**
 * The colour red.
 */
static constexpr auto RED = colour{1, 0, 0, 1};

/**
 * The colour white.
 */
static constexpr auto WHITE = colour{1, 1, 1, 1};
}

#endif //EZGL_COLOUR_HPP
