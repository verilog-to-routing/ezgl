#pragma once

#include "ezgl/qt/render_backend.hpp"

#include <QString>

#include <optional>

class QMainWindow;

namespace ezgl {

/// Which UI description format the main window is loaded from.
///
/// Both are supported while the Glade form is being retired: `glade` parses
/// the GTK-era XML via QtGladeLoader, `qt` loads a native Qt Designer form via
/// ezgl::UiLoader. `glade` stays the default until every consumer has
/// converted its form, so adding the native path changes no behaviour.
enum class ui_format { glade, qt };

/**
 * RAII wrapper that loads the application's main UI from a Qt resource
 * (or filesystem) path and owns the resulting QMainWindow.
 *
 * Delegates to ezgl::QtGladeLoader for Glade-format XML, or to
 * ezgl::UiLoader for a native Qt Designer form, per the ui_format
 * passed to the constructor.
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
  /// Load from the default Qt-resource path for `format`:
  /// ":/ezgl/main_glade.ui" for glade, ":/ezgl/main.ui" for qt.
  explicit MainWindow(ui_format format = ui_format::glade);

  /// Load from an explicit path. If `renderer_kind` is set, every
  /// canvas in the UI is materialised with the matching backend type
  /// (DrawingAreaWidget for immediate / deferred, RhiCanvasWidget for
  /// rhi); otherwise the loader's own default is used. `format` selects
  /// which parser reads the file.
  explicit MainWindow(const QString& uiPath,
                      std::optional<renderer_type> renderer_kind = std::nullopt,
                      ui_format format = ui_format::glade);

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
