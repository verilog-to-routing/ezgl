#ifdef EZGL_QT

#include <ezgl/qt/ezgl_qtcompat.hpp>
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


// DrawingAreaWidget
DrawingAreaWidget::DrawingAreaWidget(QWidget* parent): QWidget(parent)
{
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true); // for move events even without mouse button pressed
}

DrawingAreaWidget::~DrawingAreaWidget()
{

}

QImage* DrawingAreaWidget::createSurface() {
  if (!m_image) {
    const double dpr = devicePixelRatioF();
    const int w = std::max(1, int(width()  * dpr));
    const int h = std::max(1, int(height() * dpr));
    m_image = new QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    m_image->setDevicePixelRatio(dpr);
    m_image->fill(Qt::transparent);
  }
  return m_image;
}

void DrawingAreaWidget::setResizeCallback(std::function<void(int, int)> cb)
{
  m_resize_callback = std::move(cb);
}

void DrawingAreaWidget::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  // Recreate the backing image at the new widget size.
  delete m_image;
  m_image = nullptr;
  createSurface();
  // Notify the canvas so it can recreate its context and update the camera.
  if (m_resize_callback)
    m_resize_callback(width(), height());
}

void DrawingAreaWidget::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  // GTK fires configure-event every time the widget becomes visible, even
  // when the size has not changed.  This ensures the canvas is redrawn when
  // the window is re-shown for a new VPR stage (without a resize).
  // Mirror that behaviour: trigger the resize callback so the canvas redraws
  // with the current draw-state (camera/world already updated by VPR before
  // application::run() is called).
  // Guard against zero-size: the widget may not have its final layout size
  // yet on the very first show; resizeEvent will handle that case.
  if (width() > 0 && height() > 0 && m_resize_callback)
    m_resize_callback(width(), height());
}

void DrawingAreaWidget::paintEvent(QPaintEvent* event)
{
  {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.drawImage(rect(), *m_image, m_image->rect());
  }
  //m_image->save("my_image.png", "PNG");
}

// void DrawingAreaWidget::mousePressEvent(QMouseEvent* event)
// {
//   qDebug() << "Mouse press at" << event->pos();
// }

// void DrawingAreaWidget::mouseMoveEvent(QMouseEvent* event)
// {
//   qDebug() << "Mouse move at" << event->pos();
// }

// void DrawingAreaWidget::keyPressEvent(QKeyEvent* event)
// {
//   qDebug() << "Key pressed:" << event->key();
// }


bool GTK_IS_BUTTON(QObject* obj) {
  return qobject_cast<QPushButton*>(obj) != nullptr;
}

QWidget* gtk_application_get_active_window(Application* app)
{
  return Application::activeWindow();
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

int g_application_run(Application* app, int unsed1, int unsed2)
{
  Q_UNUSED(unsed1);
  Q_UNUSED(unsed2);
  g_debug("~~~ g_application_run");
  return app->exec();
}

void g_application_quit(Application* app)
{
  g_debug("~~~ g_application_quit");
  app->exit(0);
}

Application* gtk_application_new(const char* appName, int unused)
{
  Q_UNUSED(unused);
  g_debug("~~~ gtk_application_new RISKY");
  static int argc = 0;
  static char** argv = nullptr;
  Application* app = new Application(argc, argv);
  app->setApplicationName(appName);
  return app;
}

Application* gtk_application_new(const char* appName, int& argc, char** argv)
{
  g_debug("~~~ gtk_application_new");
  Application* app = new Application(argc, argv);
  app->setApplicationName(appName);
  return app;
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
    auto* w = qobject_cast<DrawingAreaWidget*>(obj);
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

int gtk_widget_get_allocated_width(QWidget* w) {
  return w->width();
}

int gtk_widget_get_allocated_height(QWidget* w) {
  return w->height();
}

char* gtk_combo_box_text_get_active_text(QComboBox* combo)
{
  if (!combo) {
    return nullptr;
  }

  QByteArray utf8 = combo->currentText().toUtf8();
  char* result = strdup(utf8.constData());  // caller must free()

  return result;
}

void gtk_combo_box_set_active(QComboBox* combo, int idx)
{
  combo->setCurrentIndex(idx);
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

#endif // EZGL_QT
