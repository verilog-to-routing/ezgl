#include <ezgl/qt/drawingareawidget.hpp>
#include <QPainter>

namespace ezgl {

DrawingAreaWidget::DrawingAreaWidget(QWidget* parent): QWidget(parent)
{
  setFocusPolicy(Qt::StrongFocus);
  setMouseTracking(true); // for move events even without mouse button pressed
}

QImage* DrawingAreaWidget::createSurface() {
  if (m_image.isNull()) {
    const double dpr = devicePixelRatioF();
    const int w = std::max(1, int(width()  * dpr));
    const int h = std::max(1, int(height() * dpr));
    m_image = QImage(w, h, QImage::Format_ARGB32_Premultiplied);
    m_image.setDevicePixelRatio(dpr);
    m_image.fill(Qt::transparent);
  }
  return &m_image;
}

QImage* DrawingAreaWidget::replaceSurface()
{
  m_image = QImage{};
  return createSurface();
}

void DrawingAreaWidget::resizeEvent(QResizeEvent* event)
{
  QWidget::resizeEvent(event);
  // on_resize must end the painter before destroying the image it paints on.
  emit resized(width(), height());
}

void DrawingAreaWidget::showEvent(QShowEvent* event)
{
  QWidget::showEvent(event);
  if (width() > 0 && height() > 0)
    emit resized(width(), height());
}

void DrawingAreaWidget::paintEvent(QPaintEvent* event)
{
  if (m_image.isNull())
    return;
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.drawImage(rect(), m_image, m_image.rect());
}

}


