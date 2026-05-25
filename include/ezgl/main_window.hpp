#pragma once

#include "ezgl/qt/render_backend.hpp"

#include <QString>

#include <optional>

class QMainWindow;

namespace ezgl {

/**
 * RAII wrapper that loads the application's main UI from a Qt resource
 * (or filesystem) path and owns the resulting QMainWindow.
 *
 * Internally delegates to ezgl::QtGladeLoader, which parses Glade-format
 * .ui XML and materialises the described widgets as Qt widgets.
 *
 * Ownership: the loaded QMainWindow is destroyed when this MainWindow
 * goes out of scope. Callers that need to hand the window off to an
 * owner with a longer lifetime (e.g. ezgl::application) should call
 * release() to transfer ownership; window() returns the raw pointer
 * without transferring.
 *
 * Move-only.
 */
class MainWindow {
public:
  /// Load from the default Qt-resource path (":/ezgl/main.ui").
  MainWindow();

  /// Load from an explicit path. If `renderer_kind` is set, every
  /// DrawingAreaWidget in the UI is materialised with the matching
  /// backend type (DrawingAreaWidget for immediate / deferred,
  /// RhiCanvasWidget for rhi); otherwise the loader's own default is used.
  explicit MainWindow(const QString& uiPath,
                      std::optional<renderer_type> renderer_kind = std::nullopt);

  ~MainWindow();

  MainWindow(const MainWindow&) = delete;
  MainWindow& operator=(const MainWindow&) = delete;
  MainWindow(MainWindow&& other) noexcept;
  MainWindow& operator=(MainWindow&& other) noexcept;

  /// Non-owning pointer to the loaded QMainWindow. Returns nullptr if
  /// the UI file could not be parsed.
  QMainWindow* window() const { return window_; }

  /// True iff the window was loaded successfully.
  explicit operator bool() const { return window_ != nullptr; }

  /// Transfer ownership of the loaded window to the caller. After
  /// release(), window() returns nullptr and the destructor is a no-op.
  QMainWindow* release();

private:
  QMainWindow* window_ = nullptr;
};

} // namespace ezgl
