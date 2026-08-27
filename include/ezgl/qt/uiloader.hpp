#ifndef EZGL_UILOADER_HPP
#define EZGL_UILOADER_HPP

#include "ezgl/qt/render_backend.hpp"

#include <QString>

#include <optional>

class QMainWindow;
class QWidget;

namespace ezgl {

/**
 * Name of the dynamic property that marks a placeholder widget.
 *
 * A .ui file can only name widget classes that Qt itself ships, so ezgl's own
 * widgets cannot be written there directly. Instead the form declares a plain
 * QWidget and tags it:
 *
 * @code{.xml}
 * <widget class="QWidget" name="MainCanvas">
 *   <property name="ezglWidgetClass" stdset="0">
 *     <string>DrawingAreaWidget</string>
 *   </property>
 * </widget>
 * @endcode
 *
 * `stdset="0"` is what makes it a *dynamic* property: Qt stores it with
 * QObject::setProperty instead of looking for a matching C++ setter. That is
 * what lets a form carry information Qt knows nothing about.
 *
 * resolve_ezgl_widgets() reads the tag and swaps in the real widget.
 * Recognised values: `"DrawingAreaWidget"` and `"SwitchButton"`.
 */
extern const char* const kEzglWidgetClassProperty;

/**
 * Name of the dynamic property that turns a widget into a popup panel.
 *
 * The value is the objectName of the button that opens it:
 *
 * @code{.xml}
 * <property name="ezglPopupFor" stdset="0"><string>NetMenuButton</string></property>
 * @endcode
 *
 * Two steps are needed to make a popup, and a .ui file can express neither.
 * A form can only set *properties*, and neither of these is one: `windowFlags`
 * is not a Q_PROPERTY of QWidget, and a button's menu is set through
 * QPushButton::setMenu rather than a property. So the form declares an
 * ordinary hidden QFrame with its contents laid out normally, and
 * apply_non_declarable_properties() supplies the missing two steps afterwards.
 */
extern const char* const kEzglPopupForProperty;

/**
 * Replace placeholder widgets by the real ezgl widget types.
 *
 * Walks @p root looking for widgets tagged with kEzglWidgetClassProperty. Each
 * one is destroyed and a real widget put in its place, keeping the objectName
 * (so application::find_widget still resolves it), its position in the parent
 * layout, its stacking order, and any size policy the form set explicitly.
 *
 * A canvas placeholder becomes an RhiCanvasWidget when @p renderer_kind is
 * renderer_type::rhi, and a DrawingAreaWidget otherwise. An unset
 * @p renderer_kind means rhi.
 *
 * @param root          Root of the loaded widget tree.
 * @param renderer_kind Which backend the canvas should use, if known.
 *
 * @note An unrecognised class name is reported as an error and the placeholder
 *       is left alone. It would be easy to carry on with the bare QWidget, but
 *       that produces a window that opens, reports success and never draws --
 *       far harder to diagnose than a log line.
 */
void resolve_ezgl_widgets(QWidget* root, std::optional<renderer_type> renderer_kind);

/**
 * Apply the widget properties a .ui file cannot express.
 *
 * Only widgets that ask for them are touched, via the dynamic properties
 * above; at present that means kEzglPopupForProperty alone. Idempotent, and
 * safe to call on any tree.
 *
 * @param root Root of the loaded widget tree.
 */
void apply_non_declarable_properties(QWidget* root);

/**
 * Loads a native Qt Designer .ui form.
 *
 * Deliberately a thin facade over QUiLoader rather than a subclass of it.
 * Stock Qt builds the entire widget tree; the two helpers above then fix up
 * the handful of things the .ui format cannot describe. Keeping that split
 * is what stops this class from growing into a second parser -- anything new
 * belongs in a helper, not here.
 *
 * @see ezgl::MainWindow, which is how an application normally loads its UI.
 */
class UiLoader {
public:
  /// Select which widget type a canvas placeholder resolves to. When unset,
  /// resolve_ezgl_widgets() falls back to the rhi backend.
  void setRendererType(renderer_type t) { m_renderer_type = t; }

  /**
   * Build the form at @p uiPath.
   *
   * @param uiPath A Qt resource path (":/ezgl/main.ui") or a filesystem path.
   * @return The loaded window, or nullptr if the file cannot be read, is not
   *         a valid form, or has a root that is not a QMainWindow. Every
   *         failure is logged with the reason before returning.
   *
   * @note Ownership of the returned window passes to the caller.
   */
  QMainWindow* loadFile(const QString& uiPath);

private:
  std::optional<renderer_type> m_renderer_type;
};

} // namespace ezgl

#endif // EZGL_UILOADER_HPP
