#ifndef EZGL_COLOUR_HPP
#define EZGL_COLOUR_HPP

namespace ezgl {

/**
 * Represents a colour as a mixture or red, green, and blue.
 *
 * By default, this struct will initialize to black.
 */
struct colour {
  /**
   * A red component of the colour, between 0.0 and 1.0.
   */
  double red = 0.0;

  /**
   * The green component of the colour, between 0.0 and 1.0.
   */
  double green = 0.0;

  /**
   * The blue component of the colour, between 0.0 and 1.0.
   */
  double blue = 0.0;
};

/**
 * The colour black.
 */
static constexpr auto BLACK = colour{0, 0, 0};

/**
 * The colour blue.
 */
static constexpr auto BLUE = colour{0, 0, 1};

/**
 * A blueish greyish colour.
 */
static constexpr auto BLUE_GREY = colour{102.0 / 255.0, 153.0 / 255.0, 204.0 / 255.0};

/**
 * A dark grey.
 */
static constexpr auto DARK_GREY = colour{169.0 / 255.0, 169.0 / 255.0, 169.0 / 255.0};

/**
 * The colour green.
 */
static constexpr auto GREEN = colour{0, 1, 0};

/**
 * A light grey.
 */
static constexpr auto LIGHT_GREY = colour{211.0 / 255.0, 211.0 / 255.0, 211.0 / 255.0};

/**
 * The colour red.
 */
static constexpr auto RED = colour{1, 0, 0};

/**
 * The colour white.
 */
static constexpr auto WHITE = colour{1, 1, 1};
}

#endif //EZGL_COLOUR_HPP
