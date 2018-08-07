#ifndef EZGL_COLOUR_HPP
#define EZGL_COLOUR_HPP

#include <cstdint>

namespace ezgl {

/**
 * Represents a colour as a mixture or red, green, and blue as well as the transparency level.
 *
 * Each colour channel and transparency level is an 8-bit value, ranging from 0-255.
 */
struct colour {
  /**
   * Create a colour.
   *
   * @param r The amount of red.
   * @param g The amount of green.
   * @param b The amount of blue.
   * @param a The level of transparency.
   */
  constexpr colour(std::uint_fast8_t r,
      std::uint_fast8_t g,
      std::uint_fast8_t b,
      std::uint_fast8_t a = 255) noexcept
      : red(r), green(g), blue(b), alpha(a)
  {
  }

  /**
   * A red component of the colour, between 0.0 and 1.0.
   */
  std::uint_fast8_t red;

  /**
   * The green component of the colour, between 0.0 and 1.0.
   */
  std::uint_fast8_t green;

  /**
   * The blue component of the colour, between 0.0 and 1.0.
   */
  std::uint_fast8_t blue;

  /**
   * The amount of transparency.
   */
  std::uint_fast8_t alpha;

  /**
   * Test for equality.
   */
  bool operator==(const colour &rhs) const
  {
    return red == rhs.red && green == rhs.green && blue == rhs.blue && alpha == rhs.alpha;
  }

  /**
   * Test for inequality.
   */
  bool operator!=(const colour &rhs) const
  {
    return !(rhs == *this);
  }
};
}

#endif //EZGL_COLOUR_HPP
