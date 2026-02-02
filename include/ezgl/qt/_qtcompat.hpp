#ifndef EZGL_GTKCOMPAT_HPP
#define EZGL_GTKCOMPAT_HPP

#ifdef EZGL_QT

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <iostream>

#include <QObject>
#include <QApplication>
#include <QImage>
#include <QWidget>
#include <QWindow>
#include <QComboBox>
#include <QPushButton>
#include <QDialog>
#include <QPainter>
#include <QPainterPath>
#include <QColor>
#include <QStatusBar>

#include <QMouseEvent>

// gtk to std types
using gchar = char;
using gpointer = void*;
using gboolean = int;
using gint = int;

#define G_APPLICATION
constexpr int EZGL_APPLICATION_DEFAULT_FLAGS = 0;

namespace ezgl {
class application;
}

class Application final : public QApplication {
public:
  Application(int& argc, char** argv): QApplication(argc, argv) {
    qInfo() << "Application()";
  }
  virtual ~Application() {
    qInfo() << "~Application()";
  }

  void setApp(ezgl::application* app) {
    m_app = app;
  }

protected:
  // bool eventFilter(QObject* obj, QEvent* event) override final;
  bool notify(QObject* receiver, QEvent* event) override final;

private:
  ezgl::application* m_app{nullptr};
};

// tmp solution to track lifetime
class Image : public QImage {
public:
  Image(const QString& str): QImage(str) {
    qInfo() << "Image()";
  }
  Image(int width, int height, QImage::Format format): QImage(width, height, format) {
    qInfo() << "Image()" << width << height << format;
  }

  virtual ~Image() {
    qInfo() << "~Image()";
  }
};
// tmp solution to track lifetime

class DrawingAreaWidget final : public QWidget {
  Q_OBJECT
public:
  explicit DrawingAreaWidget(QWidget* parent = nullptr);
  virtual ~DrawingAreaWidget();
  Image* createSurface();

protected:
  void paintEvent(QPaintEvent* event) override final;
  // void mousePressEvent(QMouseEvent* event) override final;
  // void mouseMoveEvent(QMouseEvent* event) override final;
  // void keyPressEvent(QKeyEvent* event) override final;

private:
  Image* m_image{nullptr};
};

class Painter : public QPainter {
private:
  static int nextid;
  static int counter;
  int m_id = 0;

  Painter(const Painter&) = delete;
  Painter& operator=(const Painter&) = delete;

public:
  Painter(Image* image): QPainter(image) {
    m_id = Painter::nextid++;
    Painter::counter++;
    //qInfo() << "Painter(" << m_id << ")";
    assert(image);
    assert(!image->isNull());
    assert(isActive());
    assert(Painter::counter == 1);
  }

  virtual ~Painter() {
    //qInfo() << "~Painter(" << m_id << ")";
    Painter::counter--;
  }
};
//

// gtk to qt types
using GObject = QObject;
using GtkWidget = QWidget;
using GtkButton = QPushButton;
using GtkComboBoxText = QComboBox;
using GtkDialog = QDialog;
using GtkApplication = Application;
using GdkWindow = QWindow;

class Pen : public QPen {
public:
  Pen(): QPen(Qt::SolidLine) {}

  void setWidth(double width) {
    QPen::setWidthF(width);
    m_width = width;
    if (!isSolid()) {
      applyNormalizedDashPattern();
    }
  }

  void setDashPattern(const QList<double>& dashPattern) {
    QPen::setStyle(Qt::CustomDashLine);
    m_dashPatternOrig = dashPattern;
    applyNormalizedDashPattern();
  }

  void setSolid() {
    if (!isSolid()) {
      QPen::setStyle(Qt::SolidLine);
      m_dashPatternOrig.clear();
      QPen::setDashPattern(m_dashPatternOrig);
      QPen::setDashOffset(0.0);
    }
  }

  bool isSolid() const { // in some reason QPen::isSOlid() doesn't return valid value
    return (style() == Qt::SolidLine);
  }

private:
  double m_width = 1.0;
  QList<double> m_dashPatternOrig;
  double m_offset = 0.0;

  void setWidthF(double width)=delete;
  void applyNormalizedDashPattern() {
    if (m_width > 1.0f) {
      QList<double> normalizedDashPattern;
      for (double p: m_dashPatternOrig) {
        // pattern[] is in “cairo units” (pixels/user space),
        // Qt expects “pen-width units”, so normalize:
        normalizedDashPattern.append(p/double(m_width));
      }
      QPen::setDashPattern(normalizedDashPattern);
    } else {
      QPen::setDashPattern(m_dashPatternOrig);
    }
  }
};

// cairo fake types
struct cairo_t {
public:
  cairo_t(Image* image): surface(image) {
    qInfo() << "~~~ cairo_t()";
  }
  ~cairo_t() {
    qInfo() << "~~~ ~cairo_t()";
  }

  void setAntialias(bool enabled) {
    if (enabled) {
      renderHints |= QPainter::Antialiasing;
    } else {
      renderHints &= ~QPainter::Antialiasing;
    }
  }

  void setSmoothPixmap(bool enabled) {
    if (enabled) {
      renderHints |= QPainter::SmoothPixmapTransform;
    } else {
      renderHints &= ~QPainter::SmoothPixmapTransform;
    }
  }

  void setColor(const QColor& color)
  {
    this->color = color;
    pen.setColor(color);
    brush.setColor(color);
  }

  QPainter::RenderHints renderHints;
  Image* surface{nullptr};
  QColor color;
  Pen pen;
  QBrush brush = QBrush(Qt::SolidPattern);
  QPainterPath path;
  QFont font;
  std::optional<QTransform> transform;
};

using cairo_surface_t = Image;

// cairo fake types
using mouse_callback_fn = void*;
using mouse_callback_fn = void*;
using key_callback_fn = void*;
// gtk fake types

#define TRUE 1
#define FALSE 0

// gtk wrapper
QWidget* GTK_WIDGET(QObject* obj);
QComboBox* GTK_COMBO_BOX(QObject* obj);
QWindow* GTK_WINDOW(QObject* obj);

bool GTK_IS_BUTTON(QObject* obj);
QWidget* gtk_application_get_active_window(Application* app);
void gtk_main();
void gtk_main_quit();

int g_application_run(Application* app, int, int);
void g_application_quit(Application* app);
Application* gtk_application_new(const char* appName, int);
Application* gtk_application_new(const char* appName, int& argc, char** argv);
void gtk_widget_destroy(QWidget* widget);
int gtk_widget_get_allocated_width(QWidget* w);
int gtk_widget_get_allocated_height(QWidget* w);
char* gtk_combo_box_text_get_active_text(QComboBox* combo);
void gtk_combo_box_set_active(QComboBox* combo, int idx);
void gtk_widget_queue_draw(QWidget* widget);

void g_free(void* ptr);

enum {
  GTK_RESPONSE_REJECT       = -2,
  GTK_RESPONSE_ACCEPT       = -3,
  GTK_RESPONSE_DELETE_EVENT = -4,
};

// gtk wrapper

// cairo wrapper
#define CAIRO_LINE_CAP_BUTT	Qt::FlatCap
#define CAIRO_LINE_CAP_ROUND Qt::RoundCap
#define CAIRO_LINE_CAP_SQUARE	Qt::SquareCap
using cairo_line_cap_t = Qt::PenCapStyle;

#define CAIRO_FONT_SLANT_NORMAL QFont::StyleNormal
#define CAIRO_FONT_SLANT_ITALIC QFont::StyleItalic
#define CAIRO_FONT_SLANT_OBLIQUE QFont::StyleOblique
using cairo_font_slant_t = QFont::Style;

#define CAIRO_FONT_WEIGHT_NORMAL QFont::Normal
#define CAIRO_FONT_WEIGHT_BOLD QFont::Bold
using cairo_font_weight_t = QFont::Weight;

// QPainter specific
void cairo_fill(cairo_t* ctx, Painter&);
void cairo_stroke(cairo_t* ctx, Painter&);
[[deprecated("OBSOLETE")]]
void cairo_paint(cairo_t* ctx, Painter&);
[[deprecated("OBSOLETE")]]
void cairo_set_source_surface(cairo_t* cairo, Image* surface, double x, double y, Painter&);
// QPainter specific

// QTransform specific
void cairo_save(cairo_t* ctx);
void cairo_restore(cairo_t* ctx);
void cairo_scale(cairo_t* ctx, double sx, double sy);
// QTransform specific


// text
struct cairo_text_extents_t {
  double x_bearing;
  double y_bearing;
  double width;
  double height;
  double x_advance;
  double y_advance;
};

struct cairo_font_extents_t {
  double ascent;
  double descent;
  double height;
  double max_x_advance;
  double max_y_advance;
};

void cairo_text_extents(cairo_t* ctx, const char* utf8, cairo_text_extents_t* extents);
void cairo_font_extents(cairo_t* ctx, cairo_font_extents_t* extents);
// text

int cairo_image_surface_get_width(cairo_surface_t* image);
int cairo_image_surface_get_height(cairo_surface_t* image);
void cairo_new_path(cairo_t* ctx);
void cairo_close_path(cairo_t* ctx);
void cairo_move_to(cairo_t* ctx, double x, double y);
void cairo_line_to(cairo_t* ctx, double x, double y);
void cairo_arc(cairo_t* cr, double xc, double yc, double radius, double angle1, double angle2);
void cairo_arc_negative(cairo_t* ctx, double xc, double yc, double radius, double angle1, double angle2);
void cairo_select_font_face(cairo_t* ctx, const char* family, cairo_font_slant_t slant, cairo_font_weight_t weight);
void cairo_set_dash(cairo_t* ctx, const double* pattern, int count, double offset);
void cairo_set_font_size(cairo_t* ctx, int size);
void cairo_set_line_width(cairo_t* ctx, int width);
void cairo_set_line_cap(cairo_t* ctx, cairo_line_cap_t cap);
void cairo_set_source_rgb(cairo_t* ctx, double r, double g, double b);
void cairo_set_source_rgba(cairo_t* ctx, double r, double g, double b, double a);
void cairo_surface_destroy(cairo_surface_t* surface);
void cairo_destroy(cairo_t* cairo);
// cairo wrapper

#define g_return_val_if_fail(expr, val)      \
do {                                         \
      if (!(expr)) {                         \
        std::cerr << "CRITICAL: '" \
        << #expr << "' failed at "           \
        << __FILE__ << ":" << __LINE__       \
        << std::endl;                        \
        return (val);                        \
  }                                          \
} while (0)

#define g_return_if_fail(expr)               \
do {                                         \
      if (!(expr)) {                         \
        std::cerr << "CRITICAL: '" \
        << #expr << "' failed at "           \
        << __FILE__ << ":" << __LINE__       \
        << std::endl;                        \
        return;                              \
  }                                          \
} while (0)

void log_message(const char* level, const char* file, int line, const char* fmt, ...);

constexpr const char* __filename_helper(const char* path)
{
  const char* file = path;
  for (const char* p = path; *p != '\0'; ++p) {
    if (*p == '/' || *p == '\\') {
      file = p + 1;
    }
  }
  return file;
}

#define __FILENAME__ (__filename_helper(__FILE__))

// Macros similar to g_info / g_warning
#define g_info(fmt, ...)    \
  log_message("INFO",    __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)

#define g_warning(fmt, ...) \
  log_message("WARNING", __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)

#define g_error(fmt, ...) \
  log_message("ERROR", __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)

#define g_debug(fmt, ...) \
  log_message("DEBUG", __FILENAME__, __LINE__, fmt, ##__VA_ARGS__)

#define TODO \
  std::cerr << "TODO!!!:" \
            << __FILENAME__ << " : " << __LINE__ << " : " << __PRETTY_FUNCTION__ \
            << std::endl; \
                                        \

#define ASSERT_TODO \
std::cerr << "ASSERT_TODO:" \
          << __FILENAME__ << " : " << __LINE__ << " : " << __PRETTY_FUNCTION__ \
          << std::endl; \
    assert(false); \

#endif // EZGL_QT
#endif // EZGL_GTKCOMPAT_HPP
