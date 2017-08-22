#ifndef EZGL_FONT_HPP
#define EZGL_FONT_HPP

#include <string>

namespace ezgl {

/**
 * The slant of the font.
 *
 * This enum is setup to match with the cairo graphics library and should not be changed.
 */
enum class font_slant : int { normal, italic, oblique };

/**
 * The weight of the font.
 *
 * This enum is setup to match with the cairo graphics library and should not be changed.
 */
enum class font_weight : int { normal, bold };

/**
 * Specifies the format of a font (other than its size);
 */
struct font_face {
  /**
   * Constructor.
   *
   * @param f Family name of the style of font.
   * @param s The slant of the font.
   * @param w The weight of the font.
   */
  font_face(std::string f, font_slant s, font_weight w) : family(std::move(f)), slant(s), weight(w)
  {
  }

  /**
   * The style of font.
   *
   * Examples include serif, sans-serif, cursive, fantasy, and monospace.
   */
  std::string family = "serif";

  /**
   * The slant of the font (e.g., for italics).
   */
  font_slant slant = font_slant::normal;

  /**
   * The weight of the font (e.g., for bold).
   */
  font_weight weight = font_weight::normal;
};
}

#endif //EZGL_FONT_HPP
