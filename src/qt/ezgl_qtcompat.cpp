#ifdef EZGL_QT

#include <ezgl/qt/ezgl_qtcompat.hpp>
#include <ezgl/callback.hpp>

#include <QMouseEvent>
#include <QKeyEvent>

DrawingAreaWidget::DrawingAreaWidget(QWidget* parent): QWidget(parent)
{
  setFixedSize(DRAWING_AREA_WIDTH, DRAWING_AREA_HEIGHT);
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true); // for move events even without mouse button pressed

  createSurface();
}

DrawingAreaWidget::~DrawingAreaWidget()
{

}

Image* DrawingAreaWidget::createSurface() {
  if (!m_image) {
    const double dpr = devicePixelRatioF();

#ifndef HARDCODE_DRAWING_AREA_SIZE
    const int w = std::max(1, int(width()  * dpr));
    const int h = std::max(1, int(height() * dpr));
#else
    const int w = DRAWING_AREA_WIDTH;
    const int h = DRAWING_AREA_HEIGHT;
#endif
    m_image = new Image(w, h, QImage::Format_ARGB32_Premultiplied);
    m_image->setDevicePixelRatio(dpr);
    m_image->fill(Qt::transparent);
  }
  return m_image;
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


// gtk wrapper
QWidget* GTK_WIDGET(QObject* obj) {
  return qobject_cast<QWidget*>(obj);
}

QComboBox* GTK_COMBO_BOX(QObject* obj) {
  return qobject_cast<QComboBox*>(obj);
}

QWidget* GTK_WINDOW(QObject* obj) {
  return qobject_cast<QWidget*>(obj);
}

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

int Painter::counter = 0;
int Painter::nextid = 0;

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
  g_debug("~~~ gtk_widget_queue_draw");
  widget->update();
}

void g_free(void* ptr)
{
  g_debug("~~~ g_free");
  free(ptr);
}
// gtk wrapper

// cairo wrapper

// QPainter specific
void cairo_fill(cairo_t* ctx, Painter& painter)
{
  // deactivate pen while fill
  painter.setPen(Qt::NoPen);

  // refresh brush
  painter.setBrush(ctx->brush);

  // draw path
  painter.drawPath(ctx->path);

  // clear path after drawing
  ctx->path = QPainterPath();
}

void cairo_stroke(cairo_t* ctx, Painter& painter)
{
  // deactivate brush while fill
  painter.setBrush(Qt::NoBrush);

  // refresh pen
  painter.setPen(ctx->pen);

  // draw stroke path
  painter.strokePath(ctx->path, ctx->pen);

  // clear path after drawing
  ctx->path = QPainterPath();
}

void cairo_paint(cairo_t* ctx, Painter& painter)
{
  painter.fillRect(painter.viewport(), ctx->color);
}

void cairo_set_source_surface(cairo_t*, Image* surface, double x, double y, Painter& painter)
{
  painter.drawImage(QPointF(x, y), *surface);
}
// QPainter specific

// QTransform specific
void cairo_save(cairo_t* ctx)
{
  ctx->transform = QTransform();
}

void cairo_restore(cairo_t* ctx)
{
  ctx->transform = std::nullopt;
}

void cairo_scale(cairo_t* ctx, double sx, double sy)
{
  assert(ctx->transform.has_value());
  ctx->transform.value().scale(sx, sy);
}

void cairo_text_extents(cairo_t* ctx, const char* utf8, cairo_text_extents_t* extents)
{
  QString text = QString::fromUtf8(utf8);
  QFontMetricsF fm(ctx->font);

  // QRectF is given in logical coords, origin at baseline (like Cairo)
  QRectF br = fm.boundingRect(text);

  extents->x_bearing = br.x();
  extents->y_bearing = br.y();
  extents->width     = br.width();
  extents->height    = br.height();

  // Advance: how much the current point moves along the baseline
  extents->x_advance = fm.horizontalAdvance(text);
  extents->y_advance = 0.0;  // Qt is horizontal layout, so y-advance is 0
}

void cairo_font_extents(cairo_t* ctx, cairo_font_extents_t* extents)
{
  QFontMetricsF fm(ctx->font);

  extents->ascent  = fm.ascent();
  extents->descent = fm.descent();
  extents->height  = fm.height();

  // rough equivalent: the maximum advance of any glyph in the font
  extents->max_x_advance = fm.maxWidth();

  // Cairo's max_y_advance is for vertical layouts. For Latin text it's 0.
  extents->max_y_advance = 0.0;
}
// text

int cairo_image_surface_get_width(cairo_surface_t* surface)
{
  return surface->width();
}

int cairo_image_surface_get_height(cairo_surface_t* surface)
{
  return surface->height();
}

void cairo_new_path(cairo_t* ctx)
{
  ctx->path = QPainterPath();
}

void cairo_close_path(cairo_t* ctx)
{
  ctx->path.closeSubpath();
}

void cairo_move_to(cairo_t* ctx, double x, double y)
{
  // Add 0.5 for extra half-pixel accuracy
  ctx->path.moveTo(x+0.5, y+0.5);
}

void cairo_line_to(cairo_t* ctx, double x, double y)
{
  // Add 0.5 for extra half-pixel accuracy
  ctx->path.lineTo(x+0.5, y+0.5);
}

void cairo_arc(cairo_t* ctx,
    double xc, double yc,
    double radius,
    double angle1, double angle2)
{
  // radians → degrees
  double startDeg = -angle1 * 180.0 / std::numbers::pi;
  double endDeg   = -angle2 * 180.0 / std::numbers::pi;

  double spanDeg = endDeg - startDeg;

  double d = radius * 2.0;
  QRectF rect(xc - radius, yc - radius, d, d);

  ctx->path.arcTo(rect, startDeg, spanDeg);
  if (ctx->transform.has_value()) {
    ctx->path = ctx->path * ctx->transform.value();
  }
}
void cairo_arc_negative(cairo_t* ctx,
    double xc, double yc,
    double radius,
    double angle1, double angle2)
{
  // radians → degrees
  double startDeg = -angle1 * 180.0 / std::numbers::pi;
  double endDeg   = -angle2 * 180.0 / std::numbers::pi;

  double spanDeg = endDeg - startDeg; // negative sweep

  double d = radius * 2.0;
  QRectF rect(xc - radius, yc - radius, d, d);

  ctx->path.arcTo(rect, startDeg, spanDeg);
  if (ctx->transform.has_value()) {
    ctx->path = ctx->path * ctx->transform.value();
  }
}

void cairo_select_font_face(cairo_t* ctx, const char* family, cairo_font_slant_t slant, cairo_font_weight_t weight)
{
  if (family) {
    ctx->font.setFamily(QString::fromUtf8(family));
  }

  ctx->font.setStyle(slant);
  ctx->font.setWeight(weight);
}

void cairo_set_dash(cairo_t* ctx, const double* pattern, int count, double offset)
{
  if (pattern == nullptr || count == 0) {
    ctx->pen.setSolid();
  } else {
    QList<double> dashes(count);
    for (int i=0; i < count; ++i) {
      dashes[i] = pattern[i];
    }
    ctx->pen.setDashPattern(dashes);
    ctx->pen.setDashOffset(offset);
  }
}

void cairo_set_font_size(cairo_t* ctx, int size)
{
  ctx->font.setPointSizeF(size);
}

void cairo_set_line_width(cairo_t* ctx, int width)
{
  ctx->pen.setWidth(width == 0 ? 1.0 : width);
}

void cairo_set_line_cap(cairo_t* ctx, cairo_line_cap_t cap) {
  ctx->pen.setCapStyle(cap);
}

void cairo_set_source_rgb(cairo_t* ctx, double r, double g, double b) {
  QColor c;
  c.setRedF(r);
  c.setGreenF(g);
  c.setBlueF(b);
  c.setAlphaF(1.0);
  ctx->setColor(c);
}

void cairo_set_source_rgba(cairo_t* ctx, double r, double g, double b, double a) {
  QColor c;
  c.setRedF(r);
  c.setGreenF(g);
  c.setBlueF(b);
  c.setAlphaF(a);
  ctx->setColor(c);
}

void cairo_surface_destroy(cairo_surface_t* surface) {
  g_debug("~~~cairo_surface_destroy");
  delete surface;
}

void cairo_destroy(cairo_t* cairo) {
  g_debug("~~~cairo_destroy");
  delete cairo;
}
// cairo wrapper

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
