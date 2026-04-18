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
 * Qt RHI GPU-backed rendering backend.
 *
 * Owns the persistent rhi_renderer and the deferred-redraw scheduling state
 * (defer/pending flags) that previously lived in canvas.
 */
class rhi_backend final : public render_backend {
public:
    rhi_backend(RhiCanvasWidget* widget,
                draw_canvas_fn   draw_callback,
                camera*          cam,
                color            background_color);
    ~rhi_backend() override;

    void redraw() override;
    void redraw_camera_only() override;
    void begin_deferred_redraw_cycle() override;
    void end_deferred_redraw_cycle() override;
    void on_resize(int w, int h) override;
    renderer* create_animation_renderer() override;
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
