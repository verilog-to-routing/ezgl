#include "ezgl/qt/rhi_backend.hpp"
#include "ezgl/qt/rhi_renderer.hpp"
#include "ezgl/logutils.hpp"

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
    q_info("The canvas will be redrawn (RHI path).");
}

void rhi_backend::redraw_camera_only()
{
    if (m_renderer && m_has_drawn_frame) {
        m_renderer->flush_mvp_only();
        m_pending_redraw      = false;
        m_pending_camera_only = false;
        m_has_drawn_frame     = true;
        q_info("The canvas overlay+MVP will be updated (camera-only RHI path).");
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
    if (!m_renderer || !m_widget)
        return {};

    m_renderer->begin_frame();
    m_draw_callback(m_renderer.get());
    m_renderer->flush();

    QImage frame = m_widget->grabFramebuffer();
    if (frame.isNull())
        return {};

    const int target_w = (w > 0) ? w : frame.width();
    const int target_h = (h > 0) ? h : frame.height();
    if (target_w != frame.width() || target_h != frame.height())
        return frame.scaled(target_w, target_h, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);

    return frame;
}

} // namespace ezgl
