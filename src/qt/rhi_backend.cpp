#include "ezgl/qt/rhi_backend.hpp"
#include "ezgl/qt/rhi_renderer.hpp"
#include "ezgl/logutils.hpp"
#include "ezgl/camera.hpp"

#include <functional>
#include <QImage>

namespace ezgl {

rhi_backend::~rhi_backend() = default;

rhi_backend::rhi_backend(RhiCanvasWidget* widget,
                         draw_canvas_fn   draw_callback,
                         camera*          cam,
                         color            background_color)
    : m_widget(widget)
    , m_draw_callback(draw_callback)
    , m_camera(cam)
    , m_bg_color(background_color.red,
                 background_color.green,
                 background_color.blue,
                 background_color.alpha)
{
}

void rhi_backend::redraw()
{
    if (!m_widget)
        return;

    using namespace std::placeholders;

    if (!m_renderer) {
        m_renderer = std::make_unique<rhi_renderer>(
            m_widget,
            std::bind(&camera::world_to_screen, m_camera, _1),
            m_camera,
            m_draw_callback,
            m_bg_color);
    } else {
        m_renderer->begin_frame();
    }

    m_draw_callback(m_renderer.get());
    m_renderer->flush();

    m_defer_redraw        = false;
    m_pending_redraw      = false;
    m_pending_camera_only = false;
    m_has_drawn_frame     = true;
    q_debug("The canvas will be redrawn (RHI path).");
}

void rhi_backend::redraw_camera_only()
{
    if (m_renderer && m_has_drawn_frame) {
        m_renderer->flush_mvp_only();
        m_pending_redraw      = false;
        m_pending_camera_only = false;
        m_has_drawn_frame     = true;
        q_debug("The canvas overlay+MVP will be updated (camera-only RHI path).");
        return;
    }
    redraw();
}

void rhi_backend::begin_deferred_redraw_cycle()
{
    m_defer_redraw        = true;
    m_pending_redraw      = false;
    m_pending_camera_only = false;
}

void rhi_backend::end_deferred_redraw_cycle()
{
    if (!m_defer_redraw)
        return;
    m_defer_redraw = false;
    if (m_pending_redraw || !m_has_drawn_frame)
        redraw();
    else if (m_pending_camera_only)
        redraw_camera_only();
    else if (m_renderer)
        redraw();
}

void rhi_backend::on_resize(int w, int h)
{
    const bool size_changed = (w != m_last_w || h != m_last_h);
    m_last_w = w;
    m_last_h = h;

    const bool can_reuse_geometry = size_changed && m_renderer && m_has_drawn_frame;
    if (m_defer_redraw) {
        if (can_reuse_geometry)
            m_pending_camera_only = true;
        else {
            m_pending_redraw      = true;
            m_pending_camera_only = false;
        }
    } else if (can_reuse_geometry) {
        redraw_camera_only();
    } else {
        redraw();
    }
}

renderer* rhi_backend::create_animation_renderer()
{
    return nullptr;
}

QImage rhi_backend::render_to_image(int w, int h)
{
    // Always render off-screen at exactly (w, h) — never grab the live
    // widget's framebuffer. Grabbing-and-scaling forces an IgnoreAspectRatio
    // resample from the on-screen widget aspect to the requested output
    // aspect, which distorts tile shapes whenever the widget aspect doesn't
    // match the requested aspect. It also forces the live renderer to paint
    // with the save-time camera state (canvas.cpp pre-mutates the camera
    // for the target dimensions), causing a visible jump on screen.
    //
    // The off-screen path uses an independent QRhi + render target, so the
    // live widget and live renderer are not touched at all.
    using namespace std::placeholders;
    rhi_renderer renderer(QSize(w, h),
                          std::bind(&camera::world_to_screen, *m_camera, _1),
                          m_camera,
                          m_draw_callback,
                          m_bg_color);
    renderer.begin_frame();
    m_draw_callback(&renderer);
    auto frame = renderer.flush_capture(m_bg_color);
    return RhiCanvasWidget::render_offscreen(w, h,
                                             std::move(frame.scene),
                                             frame.mvp,
                                             frame.visible_world,
                                             frame.overlay,
                                             frame.bg);
}

} // namespace ezgl
