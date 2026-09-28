#pragma once

#include <QWidget>
#include <QImage>

namespace ezgl {

/**
 * @brief The canvas widget for the QPainter (CPU) rendering paths.
 *
 * Hosts the off-screen QImage surface that the deferred_backend and
 * immediate_backend draw into, and blits that surface onto the widget in
 * paintEvent(). The widget owns the surface; the backends hold a borrowed
 * pointer to it.
 *
 * The surface is sized in device pixels (logical size scaled by the device
 * pixel ratio) so rendering stays crisp on high-DPI displays. Whenever the
 * widget is resized or first shown, it emits resized() so the backend can
 * recreate the surface at the new size and redraw.
 *
 * Focus and mouse tracking are enabled on construction so the widget receives
 * keyboard input and mouse-move events even when no mouse button is pressed.
 */
class DrawingAreaWidget final : public QWidget {
  Q_OBJECT
public:
  /// Enables strong focus (for keyboard input) and mouse tracking (for
  /// move events without a pressed mouse button).
  /// @param parent Owning parent widget, or nullptr.
  explicit DrawingAreaWidget(QWidget* parent = nullptr);

  /// Lazily allocates the off-screen surface if it does not yet exist, sized
  /// to the widget's current dimensions in device pixels and cleared to
  /// transparent. Returns the existing surface unchanged once allocated.
  /// @return Borrowed pointer to the owned QImage surface (never null).
  QImage* createSurface();

  /// Discards any existing surface and allocates a fresh one at the current
  /// size via createSurface(). Used on resize to size the surface anew.
  /// @return Borrowed pointer to the newly allocated QImage surface.
  QImage* replaceSurface();

signals:
  /// Emitted when the widget is resized or first shown with a non-empty size,
  /// signalling the backend to recreate the surface and redraw.
  /// @param w New logical width in pixels.
  /// @param h New logical height in pixels.
  void resized(int w, int h);

protected:
  /// Blits the off-screen surface onto the widget (antialiased), or does
  /// nothing if no surface has been allocated yet.
  void paintEvent(QPaintEvent* event) override final;

  /// Chains to the base implementation, then emits resized() so the backend
  /// can rebuild the surface at the new size.
  void resizeEvent(QResizeEvent* event) override final;

  /// Chains to the base implementation, then emits resized() if the widget
  /// already has a non-empty size, ensuring a surface exists before first paint.
  void showEvent(QShowEvent* event) override final;

private:
  QImage m_image;  ///< Owned off-screen render target; lent to the backend and blitted in paintEvent().
};

}


