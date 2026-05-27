#ifndef EZGL_QTUTILS_HPP
#define EZGL_QTUTILS_HPP

#include <QList>
#include <QObject>

class QWidget;
class QLayout;
class QBoxLayout;
class QGridLayout;

namespace ezgl {

// Set default layout content margins and item spacing.
void applyLayoutDefaults(QLayout* layout);

QWidget* grid_new();

QGridLayout* get_grid_layout(QWidget* grid_container);

QWidget* grid_get_child_at(QWidget* grid_container, int col, int row);

template<typename T>
inline T* grid_get_child_at(QWidget* grid_container, int col, int row) {
    return qobject_cast<T*>(grid_get_child_at(grid_container, col, row));
}

void grid_attach(QWidget* grid_container, QWidget* child, int col, int row, int w, int h);

void center_window(QWidget* w);

void widget_set_margin_start(QWidget*, int);
void widget_set_margin_end(QWidget*, int);
void widget_set_margin_top(QWidget*, int);
void widget_set_margin_bottom(QWidget*, int);

QList<QWidget*> widget_get_direct_children(QWidget* container);
void widget_set_halign(QWidget* w, Qt::AlignmentFlag flag);

void box_pack_start(QBoxLayout* box,
    QWidget* widget,
    bool expand,
    bool fill,
    int padding);

} // namespace ezgl

#endif // EZGL_QTUTILS_HPP
