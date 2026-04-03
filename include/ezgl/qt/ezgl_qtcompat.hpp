#ifndef EZGL_QTCOMPAT_HPP
#define EZGL_QTCOMPAT_HPP

#ifdef EZGL_QT

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <functional>

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
  Application(int& argc, char** argv);
  virtual ~Application();

  void setApp(ezgl::application* app);

protected:
  // bool eventFilter(QObject* obj, QEvent* event) override final;
  bool notify(QObject* receiver, QEvent* event) override final;

private:
  ezgl::application* m_app{nullptr};
};

class DrawingAreaWidget final : public QWidget {
  Q_OBJECT
public:
  explicit DrawingAreaWidget(QWidget* parent = nullptr);
  virtual ~DrawingAreaWidget();
  QImage* createSurface();

  // Register a callback invoked on every resize (including the initial show).
  // The canvas uses this to recreate its surface/context and update the camera.
  void setResizeCallback(std::function<void(int, int)> cb);

protected:
  void paintEvent(QPaintEvent* event) override final;
  void resizeEvent(QResizeEvent* event) override final;
  void showEvent(QShowEvent* event) override final;
  // void mousePressEvent(QMouseEvent* event) override final;
  // void mouseMoveEvent(QMouseEvent* event) override final;
  // void keyPressEvent(QKeyEvent* event) override final;

private:
  QImage* m_image{nullptr};
  std::function<void(int, int)> m_resize_callback;
};

// gtk to qt types
using GObject = QObject;
using GtkWidget = QWidget;
using GtkButton = QPushButton;
using GtkComboBoxText = QComboBox;
using GtkComboBox = QComboBox;
using GtkDialog = QDialog;
using GtkApplication = Application;
using GdkWindow = QWindow;

// cairo fake types
using mouse_callback_fn = void*;
using mouse_callback_fn = void*;
using key_callback_fn = void*;
// gtk fake types

#define TRUE 1
#define FALSE 0

// gtk wrapper
#define Q_WIDGET(w) qobject_cast<QWidget*>(w)
#define Q_COMBO_BOX(w) qobject_cast<QComboBox*>(w)
#define Q_DIALOG(w) qobject_cast<QDialog*>(w)

#define GTK_WIDGET(w) qobject_cast<QWidget*>(w)
#define GTK_COMBO_BOX(w) qobject_cast<QComboBox*>(w)
#define GTK_WINDOW(w) qobject_cast<QWidget*>(w)

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

#define QT_MIGRATION_TODO \
  std::cerr << "QT_MIGRATION_TODO!!!:" \
            << __FILENAME__ << " : " << __LINE__ << " : " << __PRETTY_FUNCTION__ \
            << std::endl; \

#define ASSERT_QT_MIGRATION_TODO \
  std::cerr << "ASSERT_QT_MIGRATION_TODO:" \
            << __FILENAME__ << " : " << __LINE__ << " : " << __PRETTY_FUNCTION__ \
            << std::endl; \
  std::exit(1);

#endif // EZGL_QT
#endif // EZGL_QTCOMPAT_HPP
