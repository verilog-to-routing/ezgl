#include "ezgl/qt/rhi_backend.hpp"
#include "ezgl/qt/rhi_renderer.hpp"
#include "ezgl/logutils.hpp"
#include "ezgl/camera.hpp"

#include <functional>
#include <QImage>

namespace ezgl {

rhi_backend::~rhi_backend() = default;

rhi_backend::rhi_backend(RhiCanvasWidget*         widget,
                         draw_canvas_fn           draw_callback,
                         decide_reuse_geometry_fn decide_reuse_geometry_callback,
                         camera*                  cam,
                         color                    background_color)
    : m_widget(widget)
    , m_draw_callback(draw_callback)
    , m_decide_reuse_geometry_callback(decide_reuse_geometry_callback)
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

    if (!m_renderer) {
        m_renderer = std::make_unique<rhi_renderer>(
            m_widget,
            m_camera,
            m_draw_callback,
            m_bg_color);
    } else {
        m_renderer->begin_frame();
    }

    m_draw_callback(m_renderer.get());
    m_renderer->flush();

    m_is_redraw_suspended        = false;
    m_is_redraw_requested        = false;
    m_is_camera_update_requested = false;
    m_has_drawn_frame            = true;
    q_debug("The canvas is redrawn (RHI path).");
}

void rhi_backend::redraw_at_view_change(view_change_reason reason)
{
    if (valid_to_reuse_geometry(reason)) {
        redraw_camera_only();
    } else {
        redraw();
    }
}

bool rhi_backend::valid_to_reuse_geometry(view_change_reason reason)
{
    // Setup is not yet complete. No valid geometry to reuse.
    if (!m_renderer || !m_has_drawn_frame) {
        return false;
    }

    // The callback function is not available. Default to reuse.
    if (!m_decide_reuse_geometry_callback)
        return true;

    // Let the client decide if the geometry can be reused.
    return m_decide_reuse_geometry_callback(reason, m_renderer.get());
}

void rhi_backend::redraw_camera_only() {
    if (m_renderer && m_has_drawn_frame) {
        m_renderer->flush_mvp_only();
        m_is_redraw_requested        = false;
        m_is_camera_update_requested = false;
        q_debug("The canvas overlay+MVP are updated (camera-only RHI path).");
    } else {
        redraw();
    }
}

void rhi_backend::suspend_redraw()
{
    m_is_redraw_suspended        = true;
    m_is_redraw_requested        = false;
    m_is_camera_update_requested = false;
}

void rhi_backend::resume_redraw()
{
    if (!m_is_redraw_suspended)
        return;
    m_is_redraw_suspended = false;
    if (m_is_redraw_requested || !m_has_drawn_frame)
        redraw();
    else if (m_is_camera_update_requested)
        redraw_camera_only();
    else
        redraw();
}

void rhi_backend::on_resize(int w, int h)
{
    const bool size_changed = (w != m_last_w || h != m_last_h);
    m_last_w = w;
    m_last_h = h;

    const bool can_reuse_geometry = size_changed && m_renderer && m_has_drawn_frame;
    if (m_is_redraw_suspended) {
        if (can_reuse_geometry)
            m_is_camera_update_requested = true;
        else {
            m_is_redraw_requested        = true;
            m_is_camera_update_requested = false;
        }
    } else if (can_reuse_geometry) {
        redraw_camera_only();
    } else {
        redraw();
    }
}

renderer* rhi_backend::create_animation_renderer()
{
    // The rhi_renderer is the live scene renderer and implements the full
    // irenderer interface. Draw calls route to one of two places:
    //   - world-space lines/rectangles/fills  → GPU tile batches (VBOs)
    //   - text, arcs, polys, screen-space primitives → m_overlay_deferred
    //     → overlay QImage composited above the GPU scene as a texture
    // Unlike the immediate/deferred backends — which paint synchronously
    // into a live QImage — animation draws here are recorded and only
    // become visible on the next flush() (i.e. the next refresh_drawing()).
    //
    // Lazily construct m_renderer (matching the redraw() path) so callers
    // never receive nullptr — same defensive pattern as
    // deferred_backend::create_animation_renderer().
    if (!m_renderer) {
        m_renderer = std::make_unique<rhi_renderer>(
            m_widget,
            m_camera,
            m_draw_callback,
            m_bg_color);
    }
    return m_renderer.get();
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
    rhi_renderer renderer(QSize(w, h),
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
