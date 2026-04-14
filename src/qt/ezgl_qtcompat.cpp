#include <ezgl/qt/ezgl_qtcompat.hpp>
#include <ezgl/qt/drawingareawidget.hpp>
#ifdef EZGL_RHI
#include <ezgl/qt/rhi_canvas_widget.hpp>
#endif
#include <ezgl/callback.hpp>

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QShowEvent>
#include <QStyleFactory>

// Application
Application::Application(int& argc, char** argv): QApplication(argc, argv) {
  qInfo() << "Application()";
  // Use Fusion style for consistent disabled-widget rendering across platforms.
  // System styles (gtk2, breeze) often don't visually distinguish disabled widgets.
  //setStyle(QStyleFactory::create("Fusion"));
}

Application::~Application() {
  qInfo() << "~Application()";
}

void Application::setApp(ezgl::application* app) {
  m_app = app;
}

void gtk_main() {
  g_debug("~~~ gtk_main");
  qApp->exec();
}

void gtk_main_quit()
{
  g_debug("~~~ gtk_main_quit");
  QApplication::quit();
}

// bool Application::eventFilter(QObject* obj, QEvent* event)
// {
//   auto* w = qobject_cast<QWidget*>(obj);
//   if (!w) return false;

//   switch (event->type()) {
//   case QEvent::KeyPress:
//     return press_key(w, static_cast<QKeyEvent*>(event), m_app);
//   case QEvent::MouseButtonPress:
//     return press_mouse(w, static_cast<QMouseEvent*>(event), m_app);
//   case QEvent::MouseButtonRelease:
//     return release_mouse(w, static_cast<QMouseEvent*>(event), m_app);
//   case QEvent::MouseMove:
//     return move_mouse(w, static_cast<QMouseEvent*>(event), m_app);
//   case QEvent::Wheel:
//     return scroll_mouse(w, static_cast<QWheelEvent*>(event), m_app);
//   }
  // return QObject::eventFilter(obj, event);
// }

bool Application::notify(QObject* obj, QEvent* event) {
    // Accept events from either widget type (QPainter path or RHI path).
    QWidget* w = qobject_cast<ezgl::DrawingAreaWidget*>(obj);
#ifdef EZGL_RHI
    if (!w)
      w = qobject_cast<ezgl::RhiCanvasWidget*>(obj);
#endif
    if (!w) {
      return QApplication::notify(obj, event);
    }

    bool consumed = false;

    switch (event->type()) {
    case QEvent::KeyPress:
      consumed = press_key(w, static_cast<QKeyEvent*>(event), m_app);
      break;

    case QEvent::MouseButtonPress:
      consumed = press_mouse(w, static_cast<QMouseEvent*>(event), m_app);
      break;

    case QEvent::MouseButtonRelease:
      consumed = release_mouse(w, static_cast<QMouseEvent*>(event), m_app);
      break;

    case QEvent::MouseMove:
      consumed = move_mouse(w, static_cast<QMouseEvent*>(event), m_app);
      break;

    case QEvent::Wheel:
      consumed = scroll_mouse(w, static_cast<QWheelEvent*>(event), m_app);
      break;

    default:
      break;
    }

    if (consumed) {
      return true; // stops normal processing
    }

    return QApplication::notify(obj, event);
}

void gtk_widget_destroy(QWidget* widget)
{
  g_debug("~~~ gtk_widget_destroy");
  if (!widget)
    return;

  widget->hide();
  widget->setParent(nullptr);
  widget->deleteLater();
}

void gtk_widget_queue_draw(QWidget* widget)
{
  widget->update();
}

void g_free(void* ptr)
{
  g_debug("~~~ g_free");
  free(ptr);
}
// gtk wrapper

// Core logging function (printf-style)
void log_message(const char* level,
    const char* file,
    int line,
    const char* fmt,
    ...)
{
  // timestamp (optional, but nice to have)
  std::time_t t = std::time(nullptr);
  std::tm tm{};
#if defined(_WIN32)
  localtime_s(&tm, &t);
#else
  localtime_r(&t, &tm);
#endif

  char time_buf[32];
  std::strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &tm);

  // prefix: time + level + file:line
  std::fprintf(stderr, "%s %s: %s:%d: ",
      time_buf, level, file, line);

  // body (printf-style)
  va_list ap;
  va_start(ap, fmt);
  std::vfprintf(stderr, fmt, ap);
  va_end(ap);

  std::fputc('\n', stderr);
}


