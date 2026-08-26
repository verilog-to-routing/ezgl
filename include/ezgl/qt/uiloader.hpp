#ifndef EZGL_UILOADER_HPP
#define EZGL_UILOADER_HPP

#include "ezgl/qt/render_backend.hpp"

#include <QString>

#include <optional>

class QMainWindow;
class QWidget;

namespace ezgl {

// Dynamic property naming the ezgl widget class a placeholder stands in for.
// A .ui file can only describe stock Qt widgets, so a canvas or a switch is
// declared as a plain QWidget carrying this marker:
//
//   <widget class="QWidget" name="MainCanvas">
//     <property name="ezglWidgetClass" stdset="0">
//       <string>DrawingAreaWidget</string>
//     </property>
//   </widget>
//
// resolve_ezgl_widgets() then swaps in the real type. Recognised values are
// "DrawingAreaWidget" and "SwitchButton".
extern const char* const kEzglWidgetClassProperty;

// Dynamic property turning a widget into a popup anchored to a button. The
// value is the objectName of the button that opens it. Neither the Qt::Popup
// window flag nor the button-to-popup link is expressible in .ui XML
// (windowFlags is not a Q_PROPERTY, and QAbstractButton has no menu property),
// so apply_non_declarable_properties() applies both after the tree is built.
extern const char* const kEzglPopupForProperty;

// Replace every placeholder carrying kEzglWidgetClassProperty by the real ezgl
// widget type, preserving objectName, size policy and layout position.
//
// The canvas placeholder resolves to RhiCanvasWidget when renderer_kind is
// renderer_type::rhi, and to DrawingAreaWidget otherwise. An unset
// renderer_kind means rhi, matching the historical loader default.
//
// An unrecognised class name is an error and leaves the placeholder in place:
// silently keeping a bare QWidget produces a window that opens but never
// draws, which is far harder to diagnose than a log line.
void resolve_ezgl_widgets(QWidget* root, std::optional<renderer_type> renderer_kind);

// Apply the widget properties a .ui file cannot express, to the widgets that
// ask for them via dynamic properties. Currently only kEzglPopupForProperty.
// Idempotent, and safe to call on any tree.
void apply_non_declarable_properties(QWidget* root);

// Loads a native Qt Designer .ui form.
//
// This is a thin facade over QUiLoader rather than a subclass of it: stock Qt
// builds the whole widget tree, and the two helpers above fix up afterwards
// the handful of things the .ui format cannot describe.
class UiLoader {
public:
  // Selects which widget type a canvas placeholder resolves to. When unset,
  // resolve_ezgl_widgets() falls back to the rhi backend.
  void setRendererType(renderer_type t) { m_renderer_type = t; }

  // Builds the form at uiPath, which may be a Qt resource (":/ezgl/main.ui")
  // or a filesystem path. Returns nullptr, having logged why, if the file
  // cannot be read, is not a valid form, or has a root that is not a
  // QMainWindow. Ownership of the window passes to the caller.
  QMainWindow* loadFile(const QString& uiPath);

private:
  std::optional<renderer_type> m_renderer_type;
};

} // namespace ezgl

#endif // EZGL_UILOADER_HPP
