#pragma once

#ifdef EZGL_QT

#include <QPainter>
#include <QImage>
#include <QPainterPath>
#include <QColor>
#include <QFont>

#define LINE_CAP_BUTT	Qt::FlatCap
#define LINE_CAP_ROUND Qt::RoundCap
#define LINE_CAP_SQUARE	Qt::SquareCap
using line_cap_t = Qt::PenCapStyle;

#define FONT_SLANT_NORMAL QFont::StyleNormal
#define FONT_SLANT_ITALIC QFont::StyleItalic
#define FONT_SLANT_OBLIQUE QFont::StyleOblique
using font_slant_t = QFont::Style;

#define FONT_WEIGHT_NORMAL QFont::Normal
#define FONT_WEIGHT_BOLD QFont::Bold
using font_weight_t = QFont::Weight;

class Pen : public QPen {
public:
  Pen();

  void setWidth(double width);
  void setDashPattern(const QList<double>& dashPattern);
  void setSolid();
  bool isSolid() const;

private:
  double m_width = 1.0;
  QList<double> m_dashPatternOrig;
  double m_offset = 0.0;

  void setWidthF(double width)=delete;
  void applyNormalizedDashPattern();
};

class Painter : public QPainter {
  static int s_nextid;
  static int s_counter;

  // text
  struct text_extents_t {
    double x_bearing;
    double y_bearing;
    double width;
    double height;
    double x_advance;
    double y_advance;
  };

  struct font_extents_t {
    double ascent;
    double descent;
    double height;
    double max_x_advance;
    double max_y_advance;
  };

private:
  Painter(const Painter&) = delete;
  Painter& operator=(const Painter&) = delete;

public:
  Painter(QImage* image);
  virtual ~Painter();

  void setAntialias(bool enabled);
  void setSmoothPixmap(bool enabled);
  void setColor(const QColor& color);

  // draw low level api
  void fill();
  void stroke();
  void paint();
  void set_source_surface(QImage* surface, double x, double y);
  void new_path();
  void close_path();
  void move_to(double x, double y);
  void line_to(double x, double y);
  void arc(double xc, double yc, double radius, double angle1, double angle2);
  void arc_negative(double xc, double yc, double radius, double angle1, double angle2);
  void select_font_face(const char* family, font_slant_t slant, font_weight_t weight);
  void set_dash(const double* pattern, int count, double offset);
  void set_font_size(int size);
  void set_line_width(int width);
  void set_line_cap(line_cap_t cap);
  void set_source_rgb(double r, double g, double b);
  void set_source_rgba(double r, double g, double b, double a);
  [[deprecated("use QImage not a pointer")]]
  void surface_destroy(QImage*);
  // draw low level api

  // QTransform specific
  void save();
  void restore();
  void scale(double sx, double sy);
  // QTransform specific

  // text
  void text_extents(const char* utf8, text_extents_t* extents);
  void font_extents(font_extents_t* extents);
  // text

  QImage* surface() const { return m_surface; }

private:
  int m_id = 0;
  QPainter::RenderHints m_renderHints;
  QImage* m_surface{nullptr};
  QColor m_color;
  Pen m_pen;
  QBrush m_brush = QBrush(Qt::SolidPattern);
  QPainterPath m_path;
  QFont m_font;
  std::optional<QTransform> m_transform;
};

#endif // EZGL_QT
