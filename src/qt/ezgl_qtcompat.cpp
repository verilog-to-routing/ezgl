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
#include <QLabel>
#include <QLayout>

// Application
Application::Application(int& argc, char** argv): QApplication(argc, argv) {
  qInfo() << "Application()";
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

namespace ezgl {

static QScreen* screen_for_widget(QWidget* w)
{
  if (!w)
    return QGuiApplication::primaryScreen();

  // If the widget already has a native window handle, use its actual screen.
  if (w->windowHandle() && w->windowHandle()->screen())
    return w->windowHandle()->screen();

  // Fallback: use the widget's associated screen if available.
  if (w->screen())
    return w->screen();

  return QGuiApplication::primaryScreen();
}

void center_window(QWidget* window)
{
  if (!window)
    return;

  // Important: before show(), size may not be final yet.
  // adjustSize() makes layout-based widgets get a sensible size.
  if (!window->isVisible() && !window->testAttribute(Qt::WA_Resized))
    window->adjustSize();

  QScreen* screen = screen_for_widget(window);
  if (!screen)
    return;

  // availableGeometry() excludes taskbars / system UI.
  const QRect avail = screen->availableGeometry();

  // frameGeometry() is better than width()/height() because it includes
  // the outer window frame for top-level widgets.
  QRect frame = window->frameGeometry();

  // If the frame size is still empty, fall back to sizeHint().
  if (frame.size().isEmpty()) {
    const QSize sz = window->sizeHint().isValid() ? window->sizeHint() : QSize(400, 300);
    frame.setSize(sz);
  }

  frame.moveCenter(avail.center());
  window->move(frame.topLeft());
}

void widget_set_margin_start(QWidget* w, int m)
{
  if (!w) {
    return;
  }

  QMargins margins = w->contentsMargins();
  margins.setLeft(m);
  w->setContentsMargins(margins);
}

void widget_set_margin_end(QWidget* w, int m)
{
  if (!w) {
    return;
  }

  QMargins margins = w->contentsMargins();
  margins.setRight(m);
  w->setContentsMargins(margins);
}

void widget_set_margin_top(QWidget* w, int m)
{
  if (!w) {
    return;
  }

  QMargins margins = w->contentsMargins();
  margins.setTop(m);
  w->setContentsMargins(margins);
}

void widget_set_margin_bottom(QWidget* w, int m)
{
  if (!w) {
    return;
  }

  QMargins margins = w->contentsMargins();
  margins.setBottom(m);
  w->setContentsMargins(margins);
}

QList<QWidget*> widget_get_direct_children(QWidget* container)
{
  return container->findChildren<QWidget*>(
      QString(),
      Qt::FindDirectChildrenOnly
      );
}

void widget_set_halign(QWidget* w, Qt::AlignmentFlag flag)
{
  if (auto* label = qobject_cast<QLabel*>(w)) {
    label->setAlignment((label->alignment() & ~Qt::AlignHorizontal_Mask) | flag);
  } else if (w->parentWidget() && w->parentWidget()->layout()) {
    w->parentWidget()->layout()->setAlignment(w, flag);
  }
}

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


