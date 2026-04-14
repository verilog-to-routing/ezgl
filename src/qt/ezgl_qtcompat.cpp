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
  setStyle(QStyleFactory::create("Fusion"));
}

Application::~Application() {
  qInfo() << "~Application()";
}

void Application::setApp(ezgl::application* app) {
  m_app = app;
}

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


