#include "ezgl/main_window.hpp"

#include "ezgl/qt/uiloader.hpp"

#include <QMainWindow>

namespace ezgl {

namespace {

// Resource path of the UI description loaded by the default-constructed MainWindow.
constexpr const char* kDefaultUiPath = ":/ezgl/main.ui";

// Build a QMainWindow from the UI file at path using UiLoader. When a
// renderer kind is given, it is applied to the loader (selecting the canvas
// backend) before the file is parsed. Returns the loaded top-level window.
QMainWindow* loadWith(const QString& path, std::optional<renderer_type> renderer_kind)
{
  UiLoader loader;
  if (renderer_kind.has_value()) {
    loader.setRendererType(*renderer_kind);
  }
  return loader.loadFile(path);
}

} // namespace

MainWindow::MainWindow()
    : window_(loadWith(QString::fromLatin1(kDefaultUiPath), std::nullopt))
{
}

MainWindow::MainWindow(const QString& uiPath, std::optional<renderer_type> renderer_kind)
    : window_(loadWith(uiPath, renderer_kind))
{
}

MainWindow::~MainWindow()
{
  delete window_;
}

MainWindow::MainWindow(MainWindow&& other) noexcept
    : window_(other.window_)
{
  other.window_ = nullptr;
}

MainWindow& MainWindow::operator=(MainWindow&& other) noexcept
{
  if (this != &other) {
    delete window_;
    window_ = other.window_;
    other.window_ = nullptr;
  }
  return *this;
}

QMainWindow* MainWindow::release()
{
  QMainWindow* w = window_;
  window_ = nullptr;
  return w;
}

} // namespace ezgl
