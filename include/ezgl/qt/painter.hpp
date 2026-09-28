#pragma once

#include <QPainter>
#include <QImage>
#include <QPainterPath>
#include <QColor>
#include <QFont>

namespace ezgl {

/**
 * @brief A QPen that accepts dash patterns in user-space (pixel) units.
 *
 * Qt specifies custom dash patterns in pen-width-relative units, whereas the
 * Cairo-derived drawing API used by Painter supplies them in pixels. This
 * subclass stores the original pixel-unit pattern and re-normalizes it by the
 * pen width (for widths > 1px) whenever the width or pattern changes, so the
 * dashing stays visually consistent regardless of line width.
 */
class Pen : public QPen {
public:
  /// Constructs a solid-line pen with a flat cap style.
  Pen();

  /// Sets the pen width (in pixels) and re-normalizes the dash pattern if the
  /// pen is currently dashed. @param width Line width in pixels.
  void setWidth(double width);

  /// Switches the pen to a custom dash line, storing @p dashPattern (in pixel
  /// units) and normalizing it to Qt's pen-width-relative units.
  ///
  /// The entries are lengths along the line, alternating drawn and skipped,
  /// repeating until the line ends. For example @c {5.0, 3.0} draws 5px,
  /// skips 3px, and repeats.
  ///
  /// @param dashPattern Alternating on/off dash lengths, in pixels.
  void setDashPattern(const QList<double>& dashPattern);

  /// Reverts the pen to a solid line, discarding any stored dash pattern.
  void setSolid();

  /// @return true if the pen currently draws a solid line. Reimplemented
  /// because QPen's own solidity check is unreliable here.
  bool isSolid() const;

private:
  double m_width = 1.0;            ///< Current pen width in pixels.
  QList<double> m_dashPatternOrig; ///< Dash pattern as supplied, in pixel units (pre-normalization).
  double m_offset = 0.0;           ///< Dash offset.

  /// Hidden: callers must go through setWidth() so the dash pattern is
  /// re-normalized alongside the width.
  void setWidthF(double width)=delete;

  /// Converts m_dashPatternOrig from pixel units to Qt's pen-width-relative
  /// units (dividing by the pen width when it exceeds 1px) and applies it.
  void applyNormalizedDashPattern();
};

/**
 * @brief Glyph-string metrics, mirroring Cairo's @c cairo_text_extents_t.
 *
 * Field names are carried over from Cairo; they are filled from a QFontMetricsF
 * bounding box and advance. Think of writing on lined paper: the baseline is
 * the ruled line the letters sit on, and the text origin is the spot on it
 * where you put the pen down before the first letter. @c x_bearing /
 * @c y_bearing give the offset from that spot to the inked bounding box, and
 * @c width / @c height its size. @c x_advance is where the pen ends up once
 * the string is written, i.e. the origin you would pass to draw the next
 * string so it follows on. That is usually a little more than @c width,
 * because the advance also covers the blank side bearings and any trailing
 * space, which leave no ink in the bounding box.
 *
 * @verbatim
 *    |<-x_bearing->|<------ width ------>|
 *    |             +---------------------+
 *    |             |       ink box       |
 * ---O-------------+---------------------+-----O---  baseline
 *    |                                         |
 *    |<-------------- x_advance -------------->|
 *  origin                                 next origin
 * @endverbatim
 *
 * Layout here is always horizontal, so @c y_advance is always 0.
 */
struct text_extents_t {
  double x_bearing;  ///< Horizontal offset from the text origin to the bounding box's left edge.
  double y_bearing;  ///< Vertical offset from the baseline to the bounding box's top edge.
  double width;      ///< Bounding-box width.
  double height;     ///< Bounding-box height.
  double x_advance;  ///< Horizontal pen advance: x offset from this string's origin to the next one's.
  double y_advance;  ///< Vertical pen advance; always 0 for horizontal layout.
};

/**
 * @brief Font-wide metrics, mirroring Cairo's @c cairo_font_extents_t.
 *
 * These describe the font itself, not any particular string — unlike
 * @ref text_extents_t, whose @c height is one string's bounding box.
 *
 * Field names are carried over from Cairo and filled from a QFontMetricsF.
 * @c max_y_advance applies to vertical layouts and is always 0 here.
 */
struct font_extents_t {
  double ascent;        ///< Distance the font extends above the baseline.
  double descent;       ///< Distance the font extends below the baseline.
  /// Line height: where to put the *next* line's baseline when stacking several
  /// lines. One string sits on one baseline; this spaces it from the following one.
  double height;
  double max_x_advance; ///< Maximum horizontal advance of any glyph in the font.
  double max_y_advance; ///< Maximum vertical advance; always 0 for horizontal layout.
};

/**
 * @brief A Cairo-style immediate-mode drawing facade over QPainter.
 *
 * Painter wraps a QPainter bound to an off-screen QImage and exposes a
 * Cairo-derived API: a "current source" color (set_source_rgb / set_source_rgba),
 * a "current path" built incrementally with move_to / line_to / arc, and
 * fill() / stroke() verbs that commit that path. This mirrors the original
 * Cairo back end so the rest of the library could keep its drawing logic
 * largely unchanged after the Qt migration.
 *
 * A *path* is a shape you trace out before any ink appears — think of moving a
 * pen over paper without pressing down. move_to lifts the pen to a point and
 * line_to traces across to the next. Only stroke() (ink the outline) or fill()
 * (colour the area inside it) actually draws, and doing so clears the path so
 * the next shape starts empty:
 *
 *     move_to(0,0); line_to(10,0); line_to(10,10); close_path();
 *     fill();   // the triangle appears only now
 *
 * Non-copyable: it owns an active QPainter on the target image.
 */
class Painter : public QPainter {
private:
  Painter(const Painter&) = delete;
  Painter& operator=(const Painter&) = delete;

public:
  /// Begins painting on @p image. The image must be non-null and non-empty;
  /// the QPainter is expected to become active.
  /// @param image Off-screen render target (not owned).
  Painter(QImage* image);

  /// Ends painting on the target image.
  virtual ~Painter();

  /// Enables or disables antialiasing in the pending render hints.
  void setAntialias(bool enabled);

  /// Enables or disables smooth (interpolated) pixmap/image transforms.
  void setSmoothPixmap(bool enabled);

  /// Sets the current source color, applied to both the pen and the brush.
  void setColor(const QColor& color);

  /// @name Low-level Cairo-style drawing API
  /// A path/source drawing model carried over from the Cairo back end: build a
  /// path with new_path / move_to / line_to / arc, then commit it with fill()
  /// or stroke(). Coordinates are in device pixels.
  /// @{

  /// Fills the current path with the current source color, then clears the path.
  void fill();

  /// Strokes the current path with the current pen, then clears the path.
  void stroke();

  /// Fills the entire viewport with the current source color.
  void paint();

  /// Blits @p surface onto the target image at (@p x, @p y). Mirrors Cairo's
  /// @c cairo_set_source_surface (the image is painted immediately, not stored
  /// as a deferred source).
  /// @param surface Image to draw (not owned).
  /// @param x       Destination x in device pixels.
  /// @param y       Destination y in device pixels.
  void set_source_surface(QImage* surface, double x, double y);

  /// Discards the current path, starting a fresh empty one.
  void new_path();

  /// Closes the current subpath back to its start point.
  void close_path();

  /// Begins a new subpath at (@p x, @p y).
  void move_to(double x, double y);

  /// Adds a straight segment from the current point to (@p x, @p y).
  void line_to(double x, double y);

  /// Appends a circular arc to the current path. The sweep direction follows
  /// the sign of the (angle2 - angle1) span, so this draws either direction —
  /// no separate "negative" variant is needed (unlike Cairo, which auto-wraps
  /// angles and therefore split this into cairo_arc / cairo_arc_negative).
  /// @param xc,yc        Arc center, in device pixels.
  /// @param radius       Arc radius, in device pixels.
  /// @param angle1,angle2 Start and end angles, in radians.
  void arc(double xc, double yc, double radius, double angle1, double angle2);

  /// Selects the current font family, slant, and weight for subsequent text.
  /// @param family UTF-8 family name, or nullptr to keep the current family.
  void select_font_face(const char* family, QFont::Style slant, QFont::Weight weight);

  /// Sets the line dash pattern, or reverts to a solid line.
  /// @param pattern Alternating on/off lengths in pixels, or nullptr for solid.
  /// @param count   Number of entries in @p pattern; 0 means solid.
  /// @param offset  Distance into the pattern at which dashing starts.
  void set_dash(const double* pattern, int count, double offset);

  /// Sets the font size, interpreted as pixels (minimum 1).
  void set_font_size(int size);

  /// Sets the pen line width in pixels (a width of 0 is treated as 1).
  void set_line_width(int width);

  /// Sets the pen's line cap style.
  void set_line_cap(Qt::PenCapStyle cap);

  /// Sets the current source to an opaque RGB color. Components in [0, 1].
  void set_source_rgb(double r, double g, double b);

  /// Sets the current source to an RGBA color. Components in [0, 1].
  void set_source_rgba(double r, double g, double b, double a);
  /// @}

  /// @name Text metrics
  /// @{

  /// Measures @p utf8 in the current font, mirroring Cairo's
  /// @c cairo_text_extents.
  /// @param utf8    UTF-8 string to measure.
  /// @param extents Output bounding box and advance. @see text_extents_t
  void text_extents(const char* utf8, text_extents_t* extents);

  /// Reports the current font's metrics, mirroring Cairo's
  /// @c cairo_font_extents.
  /// @param extents Output font metrics. @see font_extents_t
  void font_extents(font_extents_t* extents);
  /// @}

private:
  QPainter::RenderHints m_renderHints;          ///< Pending antialias/smoothing hints toggled by the setters.
  QColor m_color;                               ///< Current source color.
  Pen m_pen;                                    ///< Pen used by stroke(); holds width, cap, and dash state.
  QBrush m_brush = QBrush(Qt::SolidPattern);    ///< Brush used by fill().
  QPainterPath m_path;                          ///< Current path accumulated by move_to / line_to / arc.
  QFont m_font;                                 ///< Current font for text rendering and metrics.
};

} // namespace ezgl


