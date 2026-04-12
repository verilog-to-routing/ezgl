#pragma once

#include <QPainter>
#include <QImage>
#include <QPainterPath>
#include <QColor>
#include <QFont>

namespace ezgl {

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

class Painter : public QPainter {
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
  void select_font_face(const char* family, QFont::Style slant, QFont::Weight weight);
  void set_dash(const double* pattern, int count, double offset);
  void set_font_size(int size);
  void set_line_width(int width);
  void set_line_cap(Qt::PenCapStyle cap);
  void set_source_rgb(double r, double g, double b);
  void set_source_rgba(double r, double g, double b, double a);
  [[deprecated]]
  void surface_destroy(QImage* surface);
  // draw low level api

  // text
  void text_extents(const char* utf8, text_extents_t* extents);
  void font_extents(font_extents_t* extents);
  // text

private:
  QPainter::RenderHints m_renderHints;
  QColor m_color;
  Pen m_pen;
  QBrush m_brush = QBrush(Qt::SolidPattern);
  QPainterPath m_path;
  QFont m_font;
};

} // namespace ezgl


