#include "ezgl/qt/uiloader.hpp"

#include "ezgl/logutils.hpp"
#include "ezgl/qt/drawingareawidget.hpp"
#include "ezgl/qt/rhi_canvas_widget.hpp"
#include "ezgl/qt/switchbutton.hpp"

#include <QAbstractButton>
#include <QFile>
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
    // An unset renderer kind means rhi, which is the historical default.
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

// Put `real` in `placeholder`'s cell of `layout`, keeping its name.
//
// The size policy is copied only when the form set one. A placeholder left at
// QWidget's default (Preferred, Preferred) means the form said nothing, and
// copying it would clobber a policy the widget sets for itself -- SwitchButton,
// for instance, is deliberately Fixed, and would come out stretchable.
void replace_placeholder(QWidget* real, QWidget* placeholder, QLayout* layout)
{
  real->setObjectName(placeholder->objectName());

  const QSizePolicy declared = placeholder->sizePolicy();
  if (declared != QSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred)) {
    real->setSizePolicy(declared);
  }

  delete layout->replaceWidget(placeholder, real);
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

    QWidget* parent = placeholder->parentWidget();
    QLayout* layout = parent ? parent->layout() : nullptr;
    if (!layout) {
      q_error("widget %s is not in a layout; ezgl placeholders must be; leaving it in place",
          qPrintable(placeholder->objectName()));
      continue;
    }

    const QString class_name = marker.toString();
    QWidget* real = make_ezgl_widget(class_name, renderer_kind, parent);
    if (!real) {
      q_error("unknown %s value \"%s\" on widget %s; leaving the placeholder in place",
          kEzglWidgetClassProperty,
          qPrintable(class_name),
          qPrintable(placeholder->objectName()));
      continue;
    }

    replace_placeholder(real, placeholder, layout);

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
