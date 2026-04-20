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
    immediate_backend(QWidget*       drawing_area,
                      draw_canvas_fn draw_callback,
                      camera*        cam,
                      color          background_color);
    ~immediate_backend() override;

    void redraw() override;
    void redraw_camera_only() override;
    void on_resize(int w, int h) override;
    renderer* create_animation_renderer() override;
    QImage render_to_image(int w, int h) override;

private:
    void recreate_surface();

    QWidget*            m_drawing_area;
    draw_canvas_fn      m_draw_callback;
    camera*             m_camera;
    color               m_background_color;

    QImage*             m_surface  = nullptr;
    Painter*            m_painter  = nullptr;
    immediate_renderer* m_renderer = nullptr;
};

} // namespace ezgl
