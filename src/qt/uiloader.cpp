#include "ezgl/qt/uiloader.hpp"

#include "ezgl/logutils.hpp"
#include "ezgl/qt/drawingareawidget.hpp"
#include "ezgl/qt/rhi_canvas_widget.hpp"
#include "ezgl/qt/switchbutton.hpp"

#include <QAbstractButton>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLayout>
#include <QMainWindow>
#include <QUiLoader>
#include <QVariant>
#include <QWidget>

namespace ezgl {

const char* const kEzglWidgetClassProperty = "ezglWidgetClass";
const char* const kEzglPopupForProperty    = "ezglPopupFor";

namespace {

// Build the widget named by an ezglWidgetClass marker, or nullptr if the name
// is not one we know.
QWidget* make_ezgl_widget(const QString& class_name,
    std::optional<renderer_type> renderer_kind,
    QWidget* parent)
{
  if (class_name == QLatin1String("DrawingAreaWidget")) {
    // An unset renderer kind means rhi: that was QtGladeLoader's default.
    if (renderer_kind.value_or(renderer_type::rhi) == renderer_type::rhi) {
      return new RhiCanvasWidget(parent);
    }
    return new DrawingAreaWidget(parent);
  }

  if (class_name == QLatin1String("SwitchButton")) {
    return new SwitchButton(parent);
  }

  return nullptr;
}

// Carry across the state the real widget cannot rediscover for itself.
//
// Deliberately short: focus policy and mouse tracking are set by the canvas
// constructors, so only what the *form* owns needs copying. Every addition
// here is a property that would otherwise be silently lost, so keep the list
// honest rather than convenient.
void adopt_placeholder_state(QWidget* real, const QWidget* placeholder)
{
  real->setObjectName(placeholder->objectName());
  real->setEnabled(placeholder->isEnabled());

  // Only override the real widget's own size policy when the form actually
  // asked for one. A placeholder left at QWidget's default (Preferred,
  // Preferred) means the form said nothing, and copying it would clobber a
  // policy the widget sets for itself -- SwitchButton, for instance, is
  // deliberately Fixed, and would come out stretchable.
  const QSizePolicy declared = placeholder->sizePolicy();
  const QSizePolicy widget_default(QSizePolicy::Preferred, QSizePolicy::Preferred);
  if (declared != widget_default) {
    real->setSizePolicy(declared);
  }

  if (placeholder->minimumSize() != QSize(0, 0)) {
    real->setMinimumSize(placeholder->minimumSize());
  }
  if (placeholder->maximumSize() != QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX)) {
    real->setMaximumSize(placeholder->maximumSize());
  }
}

// The widget that sits directly above `w` in its parent's stacking order, or
// nullptr if `w` is topmost. Qt derives z-order from the parent's child list,
// so this is simply the next QWidget sibling.
QWidget* next_in_stacking_order(QWidget* w)
{
  QWidget* parent = w->parentWidget();
  if (!parent) {
    return nullptr;
  }

  const QObjectList& siblings = parent->children();
  for (int i = siblings.indexOf(w) + 1; i > 0 && i < siblings.size(); ++i) {
    if (QWidget* sibling = qobject_cast<QWidget*>(siblings.at(i))) {
      return sibling;
    }
  }
  return nullptr;
}

// Put `real` where `placeholder` sat. Widgets in a layout keep their cell and
// span via QLayout::replaceWidget; a widget positioned absolutely (legal in
// Designer, though none of our forms do it) falls back to copying geometry.
void take_placeholder_position(QWidget* real, QWidget* placeholder)
{
  QWidget* parent = placeholder->parentWidget();
  QLayout* layout = parent ? parent->layout() : nullptr;

  // Capture this before the swap: a newly constructed widget is appended to
  // the parent's child list, which puts it at the *top* of the z-order rather
  // than where the placeholder was. That is invisible until two widgets
  // overlap, at which point it silently decides which one the user sees.
  QWidget* stacked_above = next_in_stacking_order(placeholder);

  if (layout) {
    delete layout->replaceWidget(placeholder, real);
  } else {
    q_warning("widget %s is not in a layout; copying geometry instead",
        qPrintable(placeholder->objectName()));
    real->setGeometry(placeholder->geometry());
  }

  if (stacked_above) {
    real->stackUnder(stacked_above);
  }

  // isHidden(), not isVisible(): before the window is shown every widget
  // reports isVisible() == false, so copying that would explicitly hide the
  // real widget and it would never appear.
  real->setHidden(placeholder->isHidden());
}

} // namespace

void resolve_ezgl_widgets(QWidget* root, std::optional<renderer_type> renderer_kind)
{
  return_if_fail("resolve_ezgl_widgets root", root != nullptr);

  // findChildren() returns a snapshot, so replacing widgets while walking it
  // is safe: the widgets we create are not in the list, and each placeholder
  // appears exactly once.
  for (QWidget* placeholder : root->findChildren<QWidget*>()) {
    const QVariant marker = placeholder->property(kEzglWidgetClassProperty);
    if (!marker.isValid()) {
      continue;
    }

    const QString class_name = marker.toString();
    QWidget* real = make_ezgl_widget(class_name, renderer_kind, placeholder->parentWidget());
    if (!real) {
      q_error("unknown %s value \"%s\" on widget %s; leaving the placeholder in place",
          kEzglWidgetClassProperty,
          qPrintable(class_name),
          qPrintable(placeholder->objectName()));
      continue;
    }

    adopt_placeholder_state(real, placeholder);
    take_placeholder_position(real, placeholder);

    placeholder->setParent(nullptr);
    delete placeholder;
  }
}

void apply_non_declarable_properties(QWidget* root)
{
  return_if_fail("apply_non_declarable_properties root", root != nullptr);

  for (QWidget* popup : root->findChildren<QWidget*>()) {
    const QVariant marker = popup->property(kEzglPopupForProperty);
    if (!marker.isValid()) {
      continue;
    }

    const QString button_name = marker.toString();
    QAbstractButton* button = root->findChild<QAbstractButton*>(button_name);
    if (!button) {
      q_error("%s on widget %s names button \"%s\", which the form does not contain",
          kEzglPopupForProperty,
          qPrintable(popup->objectName()),
          qPrintable(button_name));
      continue;
    }

    // A popup is a window, so it must leave whatever layout declared it before
    // the window flags go on -- otherwise the layout keeps reserving its space.
    if (QWidget* parent = popup->parentWidget()) {
      if (QLayout* layout = parent->layout()) {
        layout->removeWidget(popup);
      }
    }

    // Reparenting to the window (rather than leaving it top-level) ties the
    // popup's lifetime to the window; passing the flags keeps Qt::Popup, which
    // is what dismisses it on an outside click.
    popup->setParent(root, Qt::Popup | Qt::FramelessWindowHint);
    popup->hide();

    QObject::connect(button, &QAbstractButton::clicked, popup, [popup, button]() {
      if (popup->isVisible()) {
        popup->hide();
        return;
      }
      popup->move(button->mapToGlobal(QPoint(0, button->height())));
      popup->show();
      popup->raise();
      popup->activateWindow();
    });
  }
}

QMainWindow* UiLoader::loadFile(const QString& uiPath)
{
  QFile file(uiPath);
  if (!file.open(QIODevice::ReadOnly)) {
    q_error("cannot open ui file %s", qPrintable(uiPath));
    return nullptr;
  }

  QUiLoader loader;

  // Form strings carry tr() semantics. Leaving translation on would make the
  // widget text depend on the host locale, which breaks text assertions in
  // tests for reasons that have nothing to do with the form.
  loader.setTranslationEnabled(false);

  // Relative <iconset resource="..."> paths resolve against this directory.
  loader.setWorkingDirectory(QFileInfo(uiPath).absoluteDir());

  QWidget* root = loader.load(&file);
  if (!root) {
    q_error("%s is not a valid Qt Designer form: %s",
        qPrintable(uiPath), qPrintable(loader.errorString()));
    return nullptr;
  }

  resolve_ezgl_widgets(root, m_renderer_type);
  apply_non_declarable_properties(root);

  QMainWindow* window = qobject_cast<QMainWindow*>(root);
  if (!window) {
    q_error("%s has a %s at its root, expected a QMainWindow",
        qPrintable(uiPath), root->metaObject()->className());
    delete root;
    return nullptr;
  }

  return window;
}

} // namespace ezgl
