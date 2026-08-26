#include "ezgl/main_window.hpp"

#include "ezgl/qt/qtgladeloader.hpp"
#include "ezgl/qt/uiloader.hpp"

#include <QMainWindow>

namespace ezgl {

namespace {

// Resource paths of the UI descriptions loaded by a default-constructed
// MainWindow, one per format.
constexpr const char* kDefaultGladeUiPath = ":/ezgl/main_glade.ui";
constexpr const char* kDefaultQtUiPath    = ":/ezgl/main.ui";

constexpr const char* default_ui_path(ui_format format)
{
  return format == ui_format::qt ? kDefaultQtUiPath : kDefaultGladeUiPath;
}

QMainWindow* loadWith(const QString& path,
    std::optional<renderer_type> renderer_kind,
    ui_format format)
{
  if (format == ui_format::qt) {
    UiLoader loader;
    if (renderer_kind.has_value()) {
      loader.setRendererType(*renderer_kind);
    }
    return loader.loadFile(path);
  }

  QtGladeLoader loader;
  if (renderer_kind.has_value()) {
    loader.setRendererType(*renderer_kind);
  }
  return loader.loadFile(path);
}

} // namespace

MainWindow::MainWindow(ui_format format)
    : window_(loadWith(QString::fromLatin1(default_ui_path(format)), std::nullopt, format))
{
}

MainWindow::MainWindow(const QString& uiPath,
    std::optional<renderer_type> renderer_kind,
    ui_format format)
    : window_(loadWith(uiPath, renderer_kind, format))
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
