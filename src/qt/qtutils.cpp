#include <ezgl/qt/qtutils.hpp>

#include <QLabel>
#include <QLayout>
#include <QGridLayout>
#include <QApplication>
#include <QScreen>
#include <QWindow>

namespace ezgl {

QWidget* grid_new()
{
  QWidget* w = new QWidget;
  w->setLayout(new QGridLayout);
  return w;
}

QGridLayout* get_grid_layout(QWidget* grid_container)
{
  if (!grid_container) {
    return nullptr;
  }

  return qobject_cast<QGridLayout*>(grid_container->layout());
}

QWidget* grid_get_child_at(QWidget* grid_container, int col, int row)
{
  QGridLayout* grid_layout = get_grid_layout(grid_container);
  if (!grid_layout) {
    return nullptr;
  }

  for (int i = 0; i < grid_layout->count(); ++i) {
    int r, c, rs, cs;
    grid_layout->getItemPosition(i, &r, &c, &rs, &cs);

    if (r == row && c == col) {
      if (auto* item = grid_layout->itemAt(i)) {
        return item->widget();
      }
    }
  }
  return nullptr;
}

void grid_attach(QWidget* grid_container, QWidget* child, int col, int row, int w, int h)
{
  QGridLayout* grid_layout = get_grid_layout(grid_container);
  if (!grid_layout) {
    return;
  }
  grid_layout->addWidget(child, row, col, h, w);
}

static QScreen* screen_for_widget(QWidget* w)
{
  if (!w)
    return QGuiApplication::primaryScreen();

  // If the widget already has a native window handle, use its actual screen.
  if (w->windowHandle() && w->windowHandle()->screen())
    return w->windowHandle()->screen();

  // Fallback: use the widget's associated screen if available.
  if (w->screen())
    return w->screen();

  return QGuiApplication::primaryScreen();
}

void center_window(QWidget* window)
{
  if (!window)
    return;

  // Important: before show(), size may not be final yet.
  // adjustSize() makes layout-based widgets get a sensible size.
  if (!window->isVisible() && !window->testAttribute(Qt::WA_Resized))
    window->adjustSize();

  QScreen* screen = screen_for_widget(window);
  if (!screen)
    return;

  // availableGeometry() excludes taskbars / system UI.
  const QRect avail = screen->availableGeometry();

  // frameGeometry() is better than width()/height() because it includes
  // the outer window frame for top-level widgets.
  QRect frame = window->frameGeometry();

  // If the frame size is still empty, fall back to sizeHint().
  if (frame.size().isEmpty()) {
    const QSize sz = window->sizeHint().isValid() ? window->sizeHint() : QSize(400, 300);
    frame.setSize(sz);
  }

  frame.moveCenter(avail.center());
  window->move(frame.topLeft());
}

void widget_set_margin_start(QWidget* w, int m)
{
  if (!w) {
    return;
  }

  QMargins margins = w->contentsMargins();
  margins.setLeft(m);
  w->setContentsMargins(margins);
}

void widget_set_margin_end(QWidget* w, int m)
{
  if (!w) {
    return;
  }

  QMargins margins = w->contentsMargins();
  margins.setRight(m);
  w->setContentsMargins(margins);
}

void widget_set_margin_top(QWidget* w, int m)
{
  if (!w) {
    return;
  }

  QMargins margins = w->contentsMargins();
  margins.setTop(m);
  w->setContentsMargins(margins);
}

void widget_set_margin_bottom(QWidget* w, int m)
{
  if (!w) {
    return;
  }

  QMargins margins = w->contentsMargins();
  margins.setBottom(m);
  w->setContentsMargins(margins);
}

QList<QWidget*> widget_get_direct_children(QWidget* container)
{
  return container->findChildren<QWidget*>(
      QString(),
      Qt::FindDirectChildrenOnly
      );
}

void widget_set_halign(QWidget* w, Qt::AlignmentFlag flag)
{
  if (auto* label = qobject_cast<QLabel*>(w)) {
    label->setAlignment((label->alignment() & ~Qt::AlignHorizontal_Mask) | flag);
  } else if (w->parentWidget() && w->parentWidget()->layout()) {
    w->parentWidget()->layout()->setAlignment(w, flag);
  }
}

void box_pack_start(QBoxLayout* box,
    QWidget* widget,
    bool expand,
    bool fill,
    int padding)
{
  if (!box || !widget) {
    return;
  }

  int stretch = expand ? 1 : 0;

  Qt::Alignment align = Qt::Alignment();
  if (!fill) {
    align = Qt::AlignLeft;
  }

  box->addWidget(widget, stretch, align);

  if (padding > 0) {
    box->setSpacing(padding);
  }
}

} // namespace ezgl
