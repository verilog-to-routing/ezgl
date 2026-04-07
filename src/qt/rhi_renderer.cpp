#if defined(EZGL_QT) && defined(EZGL_RHI)

#include "ezgl/qt/rhi_renderer.hpp"
#include "ezgl/camera.hpp"

#include <algorithm>
#include <cmath>

namespace ezgl {

// ---- construction ----------------------------------------------------------

rhi_renderer::rhi_renderer(RhiCanvasWidget* widget,
                             transform_fn     transform,
                             camera*          cam,
                             QColor           bg_color)
    : renderer(nullptr,           // painter is wired up below
               std::move(transform),
               cam,
               nullptr)           // surface not used in Qt path
    , m_rhi_widget(widget)
    , m_bg_color(bg_color)
    , m_overlay(std::max(1, widget->width()),
                std::max(1, widget->height()),
                QImage::Format_ARGB32_Premultiplied)
    , m_overlay_painter(&m_overlay)
{
    m_overlay.fill(Qt::transparent);
    m_painter = &m_overlay_painter;
    m_overlay_painter.setAntialias(false);
    m_overlay_painter.setSmoothPixmap(false);
}

// ---- frame lifecycle -------------------------------------------------------

void rhi_renderer::begin_frame()
{
    m_lines.clear();
    m_fill_verts.clear();
    m_draw_verts.clear();

    // End painter if still active (shouldn't normally happen).
    if (m_overlay_painter.isActive())
        m_overlay_painter.end();

    // Resize overlay if the widget changed since last frame.
    const int w = std::max(1, m_rhi_widget->width());
    const int h = std::max(1, m_rhi_widget->height());
    if (m_overlay.width() != w || m_overlay.height() != h)
        m_overlay = QImage(w, h, QImage::Format_ARGB32_Premultiplied);

    m_overlay.fill(Qt::transparent);

    // Restart the overlay painter on the (possibly resized) image.
    m_overlay_painter.begin(&m_overlay);
    m_overlay_painter.setAntialias(false);
    m_overlay_painter.setSmoothPixmap(false);
}

// ---- helpers ---------------------------------------------------------------

inline LineVertex rhi_renderer::make_vertex(point2d p) const
{
    return LineVertex{
        float(p.x), float(p.y),
        current_color.red,
        current_color.green,
        current_color.blue,
        current_color.alpha
    };
}

// World→NDC matrix derived from camera state and widget dimensions.
//
// world_to_screen (affine):
//   x_s =  sx * x_world + tx
//   y_s = -sy * y_world + ty   (y-flip: world y-up → screen y-down)
//
//   sx = screen.width()  / world.width()
//   sy = screen.height() / world.height()
//   tx = screen.left()   - world.left()   * sx
//   ty = screen.top()    + world.bottom() * sy
//
// screen_to_ndc (ortho, widget pixel coords):
//   x_ndc = 2 * x_s / fw - 1
//   y_ndc = 1 - 2 * y_s / fh
//
// Combined world→NDC (column-major QMatrix4x4):
//   m(0,0) = 2*sx/fw,   m(0,3) = 2*tx/fw - 1
//   m(1,1) = 2*sy/fh,   m(1,3) = 1 - 2*ty/fh
QMatrix4x4 rhi_renderer::compute_mvp() const
{
    const float fw = float(std::max(1, m_rhi_widget->width()));
    const float fh = float(std::max(1, m_rhi_widget->height()));

    const rectangle world  = m_camera->get_world();
    const rectangle screen = m_camera->get_screen();

    const float sx = float(screen.width()  / world.width());
    const float sy = float(screen.height() / world.height());
    const float tx = float(screen.left()   - world.left()   * sx);
    const float ty = float(screen.top()    + world.bottom() * sy);

    QMatrix4x4 m;
    m.setToIdentity();
    m(0, 0) = 2.0f * sx / fw;
    m(0, 3) = 2.0f * tx / fw - 1.0f;
    m(1, 1) = 2.0f * sy / fh;
    m(1, 3) = 1.0f - 2.0f * ty / fh;
    return m;
}

void rhi_renderer::push_fill_rect(point2d p0, point2d p1)
{
    // Two counter-clockwise triangles covering the axis-aligned rectangle
    // defined by corners p0 and p1 (world coords; y-flip handled by MVP).
    //
    //  (p0.x, p1.y) --- (p1.x, p1.y)
    //       |         \       |
    //  (p0.x, p0.y) --- (p1.x, p0.y)
    const point2d a{p0.x, p0.y};
    const point2d b{p1.x, p0.y};
    const point2d c{p0.x, p1.y};
    const point2d d{p1.x, p1.y};

    m_fill_verts.push_back(make_vertex(a));
    m_fill_verts.push_back(make_vertex(b));
    m_fill_verts.push_back(make_vertex(c));

    m_fill_verts.push_back(make_vertex(b));
    m_fill_verts.push_back(make_vertex(d));
    m_fill_verts.push_back(make_vertex(c));
}

void rhi_renderer::push_draw_rect(point2d p0, point2d p1)
{
    // Four line segments (8 vertices) for the rectangle outline.
    const point2d a{p0.x, p0.y};
    const point2d b{p1.x, p0.y};
    const point2d c{p1.x, p1.y};
    const point2d d{p0.x, p1.y};

    m_draw_verts.push_back(make_vertex(a));
    m_draw_verts.push_back(make_vertex(b));

    m_draw_verts.push_back(make_vertex(b));
    m_draw_verts.push_back(make_vertex(c));

    m_draw_verts.push_back(make_vertex(c));
    m_draw_verts.push_back(make_vertex(d));

    m_draw_verts.push_back(make_vertex(d));
    m_draw_verts.push_back(make_vertex(a));
}

// ---- draw_line override ----------------------------------------------------

void rhi_renderer::draw_line(point2d start, point2d end)
{
    if (current_coordinate_system != WORLD) {
        // SCREEN mode: fall through to the QPainter overlay.
        renderer::draw_line(start, end);
        return;
    }

    // Sub-pixel LOD: skip lines whose projected screen length is < 0.4 px.
    // Compute without a full world_to_screen transform by using the camera
    // scale factors directly.
    {
        const point2d s = m_camera->get_world_scale_factor(); // world/screen
        const double dx = (end.x - start.x) / s.x;
        const double dy = (end.y - start.y) / s.y;
        if (dx * dx + dy * dy < 0.16)
            return;
    }

    // GPU path: store world-space coords — MVP handles the transform.
    // No CPU clipping: the GPU viewport clips at NDC boundaries.
    m_lines.push_back(make_vertex(start));
    m_lines.push_back(make_vertex(end));
}

// ---- fill_rectangle overrides ----------------------------------------------

void rhi_renderer::fill_rectangle(point2d start, point2d end)
{
    if (current_coordinate_system != WORLD) {
        renderer::fill_rectangle(start, end);
        return;
    }

    // Normalise corners: p0 = (min_x, min_y), p1 = (max_x, max_y) in world.
    const point2d p0{ std::min(start.x, end.x), std::min(start.y, end.y) };
    const point2d p1{ std::max(start.x, end.x), std::max(start.y, end.y) };
    push_fill_rect(p0, p1);
}

void rhi_renderer::fill_rectangle(point2d start, double width, double height)
{
    fill_rectangle(start, {start.x + width, start.y + height});
}

void rhi_renderer::fill_rectangle(rectangle r)
{
    fill_rectangle({r.left(), r.bottom()}, {r.right(), r.top()});
}

// ---- draw_rectangle overrides ----------------------------------------------

void rhi_renderer::draw_rectangle(point2d start, point2d end)
{
    if (current_coordinate_system != WORLD) {
        renderer::draw_rectangle(start, end);
        return;
    }

    const point2d p0{ std::min(start.x, end.x), std::min(start.y, end.y) };
    const point2d p1{ std::max(start.x, end.x), std::max(start.y, end.y) };
    push_draw_rect(p0, p1);
}

void rhi_renderer::draw_rectangle(point2d start, double width, double height)
{
    draw_rectangle(start, {start.x + width, start.y + height});
}

void rhi_renderer::draw_rectangle(rectangle r)
{
    draw_rectangle({r.left(), r.bottom()}, {r.right(), r.top()});
}

// ---- flush -----------------------------------------------------------------

void rhi_renderer::flush()
{
    // End the overlay painter so all QPainter commands are flushed to m_overlay.
    if (m_overlay_painter.isActive())
        m_overlay_painter.end();

    double total_mb =
        (m_lines.size() +
         m_fill_verts.size() +
         m_draw_verts.size()) * sizeof(LineVertex) / (1024.0 * 1024.0);

    std::cout << "~~~ sending to GPU " << total_mb << " mb" << std::endl;

    m_rhi_widget->set_frame_data(
        std::move(m_lines),
        std::move(m_fill_verts),
        std::move(m_draw_verts),
        compute_mvp(),
        m_overlay,
        m_bg_color);

    m_rhi_widget->update();
}

// ---- flush_mvp_only --------------------------------------------------------

void rhi_renderer::flush_mvp_only()
{
    // The overlay painter is already inactive (no begin_frame was called).
    // Vertex buffers in the widget are reused; only the MVP uniform changes.
    m_rhi_widget->set_mvp_only(compute_mvp());
    m_rhi_widget->update();
}

} // namespace ezgl

#endif // EZGL_QT && EZGL_RHI
