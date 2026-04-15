#ifndef EZGL_QTCOMPAT_HPP
#define EZGL_QTCOMPAT_HPP

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
#include <QStatusBar>

#include <QMouseEvent>
#include <QResizeEvent>

class QGridLayout;
class QBoxLayout;

namespace ezgl {
class application;
}

class Application final : public QApplication {
public:
[[deprecated("unite with ezgl::application")]]
  Application(int& argc, char** argv);
  virtual ~Application();

  void setApp(ezgl::application* app);

protected:
  // bool eventFilter(QObject* obj, QEvent* event) override final;
  bool notify(QObject* receiver, QEvent* event) override final;

private:
  ezgl::application* m_app{nullptr};
};

#define Q_WIDGET(w) qobject_cast<QWidget*>(w)
#define Q_COMBO_BOX(w) qobject_cast<QComboBox*>(w)
#define Q_DIALOG(w) qobject_cast<QDialog*>(w)
#define Q_CHECKBOX(w) qobject_cast<QCheckBox*>(w)
#define Q_BUTTON(w) qobject_cast<QAbstractButton*>(w)
#define Q_LINEEDIT(w) qobject_cast<QLineEdit*>(w)

namespace ezgl {

QWidget* grid_new();

QGridLayout* get_grid_layout(QWidget* grid_container);

QWidget* grid_get_child_at(QWidget* grid_container, int col, int row);

void grid_attach(QWidget* grid_container, QWidget* child, int col, int row, int w, int h);

void center_window(QWidget* w);

void widget_set_margin_start(QWidget*, int);
void widget_set_margin_end(QWidget*, int);
void widget_set_margin_top(QWidget*, int);
void widget_set_margin_bottom(QWidget*, int);

QList<QWidget*> widget_get_direct_children(QWidget* container);
void widget_set_halign(QWidget* w, Qt::AlignmentFlag flag);

void box_pack_start(QBoxLayout* box,
    QWidget* widget,
    bool expand,
    bool fill,
    int padding);

}

enum {
  RESPONSE_REJECT       = -2,
  RESPONSE_ACCEPT       = -3,
  RESPONSE_DELETE_EVENT = -4,
};

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
        std::exit(1);                        \
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

#endif // EZGL_QTCOMPAT_HPP
