#include <ezgl/qt/painter.hpp>

#include <numbers>

namespace ezgl {

// Pen
Pen::Pen(): QPen(Qt::SolidLine) { setCapStyle(Qt::FlatCap); }

void Pen::setWidth(double width) {
  QPen::setWidthF(width);
  m_width = width;
  if (!isSolid()) {
    applyNormalizedDashPattern();
  }
}

void Pen::setDashPattern(const QList<double>& dashPattern) {
  QPen::setStyle(Qt::CustomDashLine);
  m_dashPatternOrig = dashPattern;
  applyNormalizedDashPattern();
}

void Pen::setSolid() {
  if (!isSolid()) {
    QPen::setStyle(Qt::SolidLine);
    m_dashPatternOrig.clear();
    QPen::setDashPattern(m_dashPatternOrig);
    QPen::setDashOffset(0.0);
  }
}

bool Pen::isSolid() const { // in some reason QPen::isSolid() doesn't return valid value
  return (style() == Qt::SolidLine);
}

void Pen::applyNormalizedDashPattern() {
  if (m_width > 1.0f) {
    QList<double> normalizedDashPattern;
    for (double p: m_dashPatternOrig) {
      // m_dashPatternOrig is in pixel units (user-space); Qt expects
      // pen-width-relative units, so normalise by the pen width.
      normalizedDashPattern.append(p/double(m_width));
    }
    QPen::setDashPattern(normalizedDashPattern);
  } else {
    QPen::setDashPattern(m_dashPatternOrig);
  }
}

// Painter
Painter::Painter(QImage* image): QPainter(image) {
  assert(image);
  assert(!image->isNull());
  assert(isActive());
}

Painter::~Painter() {
}

void Painter::setAntialias(bool enabled) {
  if (enabled) {
    m_renderHints |= QPainter::Antialiasing;
  } else {
    m_renderHints &= ~QPainter::Antialiasing;
  }
}

void Painter::setSmoothPixmap(bool enabled) {
  if (enabled) {
    m_renderHints |= QPainter::SmoothPixmapTransform;
  } else {
    m_renderHints &= ~QPainter::SmoothPixmapTransform;
  }
}

void Painter::setColor(const QColor& color)
{
  m_color = color;
  m_pen.setColor(color);
  m_brush.setColor(color);
}

void Painter::fill()
{
  // deactivate pen while fill
  setPen(Qt::NoPen);

  // refresh brush
  setBrush(m_brush);

  // draw path
  drawPath(m_path);

  // clear path after drawing
  m_path = QPainterPath();
}

void Painter::stroke()
{
  // deactivate brush while fill
  setBrush(Qt::NoBrush);

  // refresh pen
  setPen(m_pen);

  // draw stroke path
  strokePath(m_path, m_pen);

  // clear path after drawing
  m_path = QPainterPath();
}

void Painter::paint()
{
  fillRect(viewport(), m_color);
}

void Painter::set_source_surface(QImage* surface, double x, double y)
{
  drawImage(QPointF(x, y), *surface);
}

void Painter::text_extents(const char* utf8, text_extents_t* extents)
{
  QString text = QString::fromUtf8(utf8);
  QFontMetricsF fm(m_font);

  // QRectF is given in logical coords, origin at baseline.
  QRectF br = fm.boundingRect(text);

  extents->x_bearing = br.x();
  extents->y_bearing = br.y();
  extents->width     = br.width();
  extents->height    = br.height();

  // Advance: how much the current point moves along the baseline
  extents->x_advance = fm.horizontalAdvance(text);
  extents->y_advance = 0.0;  // Qt is horizontal layout, so y-advance is 0
}

void Painter::font_extents(font_extents_t* extents)
{
  QFontMetricsF fm(m_font);

  extents->ascent  = fm.ascent();
  extents->descent = fm.descent();
  extents->height  = fm.height();

  // rough equivalent: the maximum advance of any glyph in the font
  extents->max_x_advance = fm.maxWidth();

  // max_y_advance is for vertical layouts. For Latin text it's 0.
  extents->max_y_advance = 0.0;
}
// text

void Painter::new_path()
{
  m_path = QPainterPath();
}

void Painter::close_path()
{
  m_path.closeSubpath();
}

void Painter::move_to(double x, double y)
{
  // Add 0.5 for extra half-pixel accuracy
  m_path.moveTo(x+0.5, y+0.5);
}

void Painter::line_to(double x, double y)
{
  // Add 0.5 for extra half-pixel accuracy
  m_path.lineTo(x+0.5, y+0.5);
}

void Painter::arc(double xc, double yc,
    double radius,
    double angle1, double angle2)
{
  // radians → degrees
  double startDeg = -angle1 * 180.0 / std::numbers::pi;
  double endDeg   = -angle2 * 180.0 / std::numbers::pi;

  double spanDeg = endDeg - startDeg;

  double d = radius * 2.0;
  QRectF rect(xc - radius, yc - radius, d, d);

  m_path.arcTo(rect, startDeg, spanDeg);
}

void Painter::arc_negative(double xc, double yc,
    double radius,
    double angle1, double angle2)
{
  // radians → degrees
  double startDeg = -angle1 * 180.0 / std::numbers::pi;
  double endDeg   = -angle2 * 180.0 / std::numbers::pi;

  double spanDeg = endDeg - startDeg; // negative sweep

  double d = radius * 2.0;
  QRectF rect(xc - radius, yc - radius, d, d);

  m_path.arcTo(rect, startDeg, spanDeg);
}

void Painter::select_font_face(const char* family, QFont::Style slant, QFont::Weight weight)
{
  if (family) {
    m_font.setFamily(QString::fromUtf8(family));
  }

  m_font.setStyle(slant);
  m_font.setWeight(weight);
  QPainter::setFont(m_font);
}

void Painter::set_dash(const double* pattern, int count, double offset)
{
  if (pattern == nullptr || count == 0) {
    m_pen.setSolid();
  } else {
    QList<double> dashes(count);
    for (int i=0; i < count; ++i) {
      dashes[i] = pattern[i];
    }
    m_pen.setDashPattern(dashes);
    m_pen.setDashOffset(offset);
  }
}

void Painter::set_font_size(int size)
{
  // Treat font size as pixels (use setPixelSize, not setPointSize).
  m_font.setPixelSize(std::max(1, size));
  QPainter::setFont(m_font);
}

void Painter::set_line_width(int width)
{
  m_pen.setWidth(width == 0 ? 1.0 : width);
}

void Painter::set_line_cap(Qt::PenCapStyle cap) {
  m_pen.setCapStyle(cap);
}

void Painter::set_source_rgb(double r, double g, double b) {
  QColor c;
  c.setRedF(r);
  c.setGreenF(g);
  c.setBlueF(b);
  c.setAlphaF(1.0);
  setColor(c);
}

void Painter::set_source_rgba(double r, double g, double b, double a) {
  QColor c;
  c.setRedF(r);
  c.setGreenF(g);
  c.setBlueF(b);
  c.setAlphaF(a);
  setColor(c);
}

} // namespace ezgl


