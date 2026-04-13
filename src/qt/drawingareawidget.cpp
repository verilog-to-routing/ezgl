#include <ezgl/qt/drawingareawidget.hpp>
#include <QPainter>

namespace ezgl {

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

void DrawingAreaWidget::setPreResizeCallback(std::function<void()> cb)
{
  m_pre_resize_callback = std::move(cb);
}

void DrawingAreaWidget::setResizeCallback(std::function<void(int, int)> cb)
{
  m_resize_callback = std::move(cb);
}

void DrawingAreaWidget::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  // End the active painter before destroying the image it is painting on.
  if (m_pre_resize_callback)
    m_pre_resize_callback();
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
}

}


