#pragma once

#include "ezgl/qt/render_backend.hpp"
#include "ezgl/camera.hpp"
#include "ezgl/color.hpp"
#include "ezgl/qt/rhi_canvas_widget.hpp"

#include <memory>
#include <QColor>

namespace ezgl {

class rhi_renderer;

/**
 * @brief Qt RHI GPU-backed @ref render_backend implementation. Lifecycle
 * wrapper around @ref rhi_renderer.
 *
 * Owns a persistent @ref rhi_renderer plus the deferred-redraw scheduling
 * state (defer / pending flags) that previously lived in @c canvas. Calls
 * @ref rhi_renderer::flush() for full redraws and
 * @ref rhi_renderer::flush_mvp_only() for camera-only redraws.
 *
 * @par Defer / pending flag state machine
 * @code
 *   begin_deferred_redraw_cycle();   // m_defer_redraw = true
 *     redraw();                      // m_pending_redraw = true (no flush yet)
 *     redraw_camera_only();          // m_pending_camera_only = true
 *     ...
 *   end_deferred_redraw_cycle();     // m_defer_redraw = false; if any
 *                                    // pending, flush once (full beats
 *                                    // camera-only — if both set, full wins).
 * @endcode
 *
 * Without the defer window, @ref redraw() and @ref redraw_camera_only()
 * dispatch immediately. The first call to either also flips
 * @c m_has_drawn_frame so subsequent resize events know whether the
 * scene has been initialised.
 *
 * @par Headless capture
 * @ref render_to_image() never touches the on-screen widget or its
 * @ref rhi_renderer. It constructs a transient @ref rhi_renderer via the
 * headless (size-based) constructor, runs the draw callback through it,
 * calls @ref rhi_renderer::flush_capture(), and passes the captured
 * frame into the static helper @ref RhiCanvasWidget::render_offscreen(),
 * which builds its own standalone @c QRhi over @c QOffscreenSurface. No
 * @ref RhiCanvasWidget instance is created.
 */
class rhi_backend final : public render_backend {
public:
    rhi_backend(RhiCanvasWidget* widget,
                draw_canvas_fn   draw_callback,
                camera*          cam,
                color            background_color);
    ~rhi_backend() override;

    /// Full redraw. Within a defer cycle, sets @c m_pending_redraw and
    /// returns immediately; otherwise calls @ref rhi_renderer::flush().
    void redraw() override;

    /// Camera-only redraw. Within a defer cycle, sets
    /// @c m_pending_camera_only; otherwise calls
    /// @ref rhi_renderer::flush_mvp_only(). If a full redraw is also
    /// pending, the full redraw wins (it produces a superset of the
    /// camera-only result).
    void redraw_camera_only() override;

    /// Open a defer window: coalesce multiple @ref redraw /
    /// @ref redraw_camera_only calls into a single GPU frame on close.
    void begin_deferred_redraw_cycle() override;

    /// Close the defer window and flush any pending redraw.
    void end_deferred_redraw_cycle() override;

    /// Resize: re-create the rhi_renderer at the new dimensions and
    /// remember the size so the next redraw uses it.
    void on_resize(int w, int h) override;

    /// Animation overlay renderer. Returns an immediate renderer painting
    /// into the widget surface on top of the cached GPU frame.
    renderer* create_animation_renderer() override;

    /// Headless PNG capture. See class brief for the offscreen flow.
    QImage render_to_image(int w, int h) override;

private:
    RhiCanvasWidget*              m_widget;
    draw_canvas_fn                m_draw_callback;
    camera*                       m_camera;
    QColor                        m_bg_color;
    std::unique_ptr<rhi_renderer> m_renderer;

    bool m_defer_redraw        = false;
    bool m_pending_redraw      = false;
    bool m_pending_camera_only = false;
    bool m_has_drawn_frame     = false;

    int m_last_w = 0;
    int m_last_h = 0;
};

} // namespace ezgl
