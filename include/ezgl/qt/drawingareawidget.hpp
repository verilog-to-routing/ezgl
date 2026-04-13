#pragma once

#include <QWidget>
#include <QImage>

namespace ezgl {

class DrawingAreaWidget final : public QWidget {
  Q_OBJECT
public:
  explicit DrawingAreaWidget(QWidget* parent = nullptr);
  virtual ~DrawingAreaWidget();
  QImage* createSurface();

  // Register a callback invoked before the backing image is destroyed on resize.
  // The canvas uses this to end its active painter before the device is deleted.
  void setPreResizeCallback(std::function<void()> cb);

  // Register a callback invoked on every resize (including the initial show).
  // The canvas uses this to recreate its surface/context and update the camera.
  void setResizeCallback(std::function<void(int, int)> cb);

protected:
  void paintEvent(QPaintEvent* event) override final;
  void resizeEvent(QResizeEvent* event) override final;
  void showEvent(QShowEvent* event) override final;

private:
  QImage* m_image{nullptr};
  std::function<void()> m_pre_resize_callback;
  std::function<void(int, int)> m_resize_callback;
};

}


