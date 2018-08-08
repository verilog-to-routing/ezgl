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

static constexpr colour WHITE(0xFF, 0xFF, 0xFF);
static constexpr colour BLACK(0x00, 0x00, 0x00);
static constexpr colour GREY_55(0x8C, 0x8C, 0x8C);
static constexpr colour GREY_75(0xBF, 0xBF, 0xBF);
static constexpr colour RED(0xFF, 0x00, 0x00);
static constexpr colour ORANGE(0xFF, 0xA5, 0x00);
static constexpr colour YELLOW(0xFF, 0xFF, 0x00);
static constexpr colour GREEN(0x00, 0xFF, 0x00);
static constexpr colour CYAN(0x00, 0xFF, 0xFF);
static constexpr colour BLUE(0x00, 0x00, 0xFF);
static constexpr colour PURPLE(0xA0, 0x20, 0xF0);
static constexpr colour PINK(0xFF, 0xC0, 0xCB);
static constexpr colour LIGHT_PINK(0xFF, 0xB6, 0xC1);
static constexpr colour DARK_GREEN(0x00, 0x64, 0x00);
static constexpr colour MAGENTA(0xFF, 0x00, 0xFF);
static constexpr colour BISQUE(0xFF, 0xE4, 0xC4);
static constexpr colour LIGHT_SKY_BLUE(0x87, 0xCE, 0xFA);
static constexpr colour THISTLE(0xD8, 0xBF, 0xD8);
static constexpr colour PLUM(0xDD, 0xA0, 0xDD);
static constexpr colour KHAKI(0xF0, 0xE6, 0x8C);
static constexpr colour CORAL(0xFF, 0x7F, 0x50);
static constexpr colour TURQUOISE(0x40, 0xE0, 0xD0);
static constexpr colour MEDIUM_PURPLE(0x93, 0x70, 0xDB);
static constexpr colour DARK_SLATE_BLUE(0x48, 0x3D, 0x8B);
static constexpr colour DARK_KHAKI(0xBD, 0xB7, 0x6B);
static constexpr colour LIGHT_MEDIUM_BLUE(0x44, 0x44, 0xFF);
static constexpr colour SADDLE_BROWN(0x8B, 0x45, 0x13);
static constexpr colour FIRE_BRICK(0xB2, 0x22, 0x22);
static constexpr colour LIME_GREEN(0x32, 0xCD, 0x32);
}

#endif //EZGL_COLOUR_HPP
