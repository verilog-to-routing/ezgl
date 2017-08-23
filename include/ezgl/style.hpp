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
}

#endif //EZGL_FONT_HPP
