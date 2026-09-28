#pragma once

#include "ezgl/qt/render_backend.hpp"
#include "ezgl/camera.hpp"
#include "ezgl/color.hpp"

#include <QImage>
#include <QWidget>

namespace ezgl {

class Painter;
class immediate_renderer;

/**
 * Immediate-mode QPainter rendering backend.
 *
 * Every draw call is executed synchronously against the active QPainter —
 * no batching or deferred dispatch.  Otherwise identical lifecycle to
 * deferred_backend (same DrawingAreaWidget, same resize handling).
 */
class immediate_backend final : public render_backend {
public:
    /**
     * @param drawing_area     Qt widget to render into (a DrawingAreaWidget,
     *                         which supplies the off-screen surface).
     * @param draw_callback    Application scene-drawing callback, invoked each
     *                         redraw.
     * @param cam              Camera supplying the view and world→screen
     *                         transform.
     * @param background_color Color the surface is cleared to before each frame.
     */
    immediate_backend(QWidget*       drawing_area,
                      draw_canvas_fn draw_callback,
                      camera*        cam,
                      color          background_color);

    /// Destroys the owned Painter and renderer. The surface is owned by the
    /// DrawingAreaWidget, so it is not freed here.
    ~immediate_backend() override;

    // render_backend interface overrides.

    /// @name Clear the surface to the background color, draw the scene directly through the immediate_renderer, and request a widget repaint.
    /// @{
    void redraw() override;
    /// @}
    /// @name The immediate path holds no scene cache, so fall through to a full redraw.
    /// @{
    void redraw_at_view_change(view_change_reason /*reason*/) override;
    /// @}
    /// @name Tear down the Painter and renderer, pull a fresh surface from the DrawingAreaWidget, and redraw (w / h unused — the widget supplies the resized surface).
    /// @{
    void on_resize(int w, int h) override;
    /// @}
    /// @name Return the backend's own immediate_renderer for animation overlays; the immediate path reuses it rather than creating a separate one.
    /// @{
    renderer* create_animation_renderer() override;
    /// @}
    /// @name Render a fresh frame at (w, h) into a new QImage via an independent Painter and immediate_renderer.
    /// @{
    QImage render_to_image(int w, int h) override;
    /// @}

private:
    /// Pulls a fresh off-screen surface from the DrawingAreaWidget and rebuilds
    /// the Painter and immediate_renderer. Called on construction and on every resize.
    void recreate_surface();

    QWidget*            m_drawing_area;     ///< Target Qt widget (not owned); the DrawingAreaWidget supplying the surface.
    draw_canvas_fn      m_draw_callback;    ///< Application scene-drawing callback, invoked each redraw.
    camera*             m_camera;           ///< Camera providing the view and world→screen transform (not owned).
    color               m_background_color; ///< Color the surface is cleared to before each frame.

    QImage*             m_surface  = nullptr; ///< Off-screen render target; owned by the DrawingAreaWidget, not freed here.
    Painter*            m_painter  = nullptr; ///< Painter wrapping m_surface; owned, recreated on resize, deleted in dtor.
    immediate_renderer* m_renderer = nullptr; ///< Immediate renderer drawing through m_painter; owned, recreated on resize, deleted in dtor.
};

} // namespace ezgl
