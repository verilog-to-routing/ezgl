#pragma once

#include <QWidget>
#include <QImage>

namespace ezgl {

class DrawingAreaWidget final : public QWidget {
  Q_OBJECT
public:
  explicit DrawingAreaWidget(QWidget* parent = nullptr);
  QImage* createSurface();
  QImage* replaceSurface();

signals:
  void resized(int w, int h);

protected:
  void paintEvent(QPaintEvent* event) override final;
  void resizeEvent(QResizeEvent* event) override final;
  void showEvent(QShowEvent* event) override final;

private:
  QImage m_image;
};

}


