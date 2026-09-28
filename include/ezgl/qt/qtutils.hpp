#ifndef EZGL_QTUTILS_HPP
#define EZGL_QTUTILS_HPP

#include <QList>
#include <QObject>

class QWidget;
class QLayout;
class QBoxLayout;
class QGridLayout;

namespace ezgl {

/**
 * @file qtutils.hpp
 * @brief Small Qt widget/layout helpers.
 *
 * Several functions here deliberately keep GTK-style names (`grid_*`,
 * `widget_*`, `box_pack_start`) so code ported from the original GTK/Glade
 * front end reads the same. Each is a thin shim over the equivalent Qt API; the
 * per-function docs note the GTK function it stands in for and how it maps onto
 * Qt's layout model.
 */

/// Applies the library's default content margins and item spacing to @p layout
/// (currently both zero), giving every layout a consistent baseline.
void applyLayoutDefaults(QLayout* layout);

/// Creates a container widget backed by a QGridLayout.
/// GTK analogue: @c gtk_grid_new (a GtkGrid is both container and layout; in Qt
/// the grid is a QLayout, so this pairs a QWidget with a fresh QGridLayout).
/// @return Newly allocated QWidget owning an empty QGridLayout.
QWidget* grid_new();

/// Returns the QGridLayout of a grid container created by grid_new().
/// @param grid_container Widget whose layout is a QGridLayout, or nullptr.
/// @return The QGridLayout, or nullptr if @p grid_container is null or not a grid.
QGridLayout* get_grid_layout(QWidget* grid_container);

/// Returns the child widget at grid cell (@p col, @p row).
/// GTK analogue: @c gtk_grid_get_child_at.
/// @param grid_container Grid container created by grid_new().
/// @param col            Zero-based column index.
/// @param row            Zero-based row index.
/// @return The widget occupying that cell, or nullptr if the cell is empty.
QWidget* grid_get_child_at(QWidget* grid_container, int col, int row);

/// Typed convenience overload of grid_get_child_at(): returns the cell's widget
/// cast to @c T*, or nullptr if the cell is empty or the type does not match.
template<typename T>
inline T* grid_get_child_at(QWidget* grid_container, int col, int row) {
    return qobject_cast<T*>(grid_get_child_at(grid_container, col, row));
}

/// Places @p child in the grid spanning @p w columns and @p h rows from cell
/// (@p col, @p row). GTK analogue: @c gtk_grid_attach. Note the GTK argument
/// order (col, row, width, height); this maps to QGridLayout::addWidget(child,
/// row, col, h, w).
/// @param grid_container Grid container created by grid_new().
/// @param child          Widget to add.
/// @param col,row        Top-left cell of the span (zero-based).
/// @param w,h            Column span and row span, in cells.
void grid_attach(QWidget* grid_container, QWidget* child, int col, int row, int w, int h);

/// Centers a top-level window on the screen it belongs to, within the available
/// (taskbar-excluding) geometry. If called before the window is shown, its size
/// is resolved via adjustSize()/sizeHint() first.
void center_window(QWidget* w);

/// Sets the leading content margin of @p w. GTK analogue:
/// @c gtk_widget_set_margin_start (mapped to the left margin, assuming LTR).
/// @param m Margin in pixels.
void widget_set_margin_start(QWidget* w, int m);
/// Sets the trailing content margin of @p w. GTK analogue:
/// @c gtk_widget_set_margin_end (mapped to the right margin, assuming LTR).
/// @param m Margin in pixels.
void widget_set_margin_end(QWidget* w, int m);
/// Sets the top content margin of @p w. GTK analogue: @c gtk_widget_set_margin_top.
/// @param m Margin in pixels.
void widget_set_margin_top(QWidget* w, int m);
/// Sets the bottom content margin of @p w. GTK analogue: @c gtk_widget_set_margin_bottom.
/// @param m Margin in pixels.
void widget_set_margin_bottom(QWidget* w, int m);

/// Returns the immediate child widgets of @p container (non-recursive).
/// GTK-style `widget_*` name; wraps QObject::findChildren with
/// Qt::FindDirectChildrenOnly.
QList<QWidget*> widget_get_direct_children(QWidget* container);

/// Sets the horizontal alignment of @p w. GTK analogue: @c gtk_widget_set_halign.
/// For a QLabel this sets the text alignment; otherwise it sets the widget's
/// alignment within its parent layout. @param flag A Qt horizontal alignment flag.
void widget_set_halign(QWidget* w, Qt::AlignmentFlag flag);

/// Adds @p widget to the end of @p box. GTK analogue: @c gtk_box_pack_start,
/// whose @p expand / @p fill / @p padding box-packing concepts map onto Qt as:
/// @param box     Target box layout.
/// @param widget  Widget to add.
/// @param expand  Whether the widget shares extra space (stretch factor 1 vs 0).
/// @param fill    If false, the widget is left-aligned rather than filling its cell.
/// @param padding If > 0, applied as the layout's item spacing.
void box_pack_start(QBoxLayout* box,
    QWidget* widget,
    bool expand,
    bool fill,
    int padding);

} // namespace ezgl

#endif // EZGL_QTUTILS_HPP
