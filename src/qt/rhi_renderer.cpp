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
    // Wire the base-class painter to our overlay image so that non-overridden
    // calls (draw_text, fill_arc, fill_poly, …) render into the overlay.
    m_painter = &m_overlay_painter;
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

inline point2d rhi_renderer::to_screen(point2d p) const
{
    if (current_coordinate_system == WORLD)
        return m_transform(p);
    return p; // SCREEN: already pixel coords
}

QMatrix4x4 rhi_renderer::compute_mvp() const
{
    const float w = float(std::max(1, m_rhi_widget->width()));
    const float h = float(std::max(1, m_rhi_widget->height()));
    // Orthographic: maps pixel (0,0)→(w,h) to NDC (-1,1)→(1,-1).
    // left=0, right=w, bottom=h, top=0 → y-flip (screen y-down → NDC y-up).
    QMatrix4x4 m;
    m.setToIdentity();
    m.ortho(0.0f, w, h, 0.0f, -1.0f, 1.0f);
    return m;
}

void rhi_renderer::push_fill_rect(point2d tl, point2d br)
{
    // Two counter-clockwise triangles:
    //  tl --- tr
    //  |  \    |
    //  bl  --- br
    point2d tr{br.x, tl.y};
    point2d bl{tl.x, br.y};

    m_fill_verts.push_back(make_vertex(tl));
    m_fill_verts.push_back(make_vertex(bl));
    m_fill_verts.push_back(make_vertex(tr));

    m_fill_verts.push_back(make_vertex(tr));
    m_fill_verts.push_back(make_vertex(bl));
    m_fill_verts.push_back(make_vertex(br));
}

void rhi_renderer::push_draw_rect(point2d tl, point2d br)
{
    // Four line segments (8 vertices).
    point2d tr{br.x, tl.y};
    point2d bl{tl.x, br.y};

    m_draw_verts.push_back(make_vertex(tl));
    m_draw_verts.push_back(make_vertex(tr));

    m_draw_verts.push_back(make_vertex(tr));
    m_draw_verts.push_back(make_vertex(br));

    m_draw_verts.push_back(make_vertex(br));
    m_draw_verts.push_back(make_vertex(bl));

    m_draw_verts.push_back(make_vertex(bl));
    m_draw_verts.push_back(make_vertex(tl));
}

// ---- draw_line override ----------------------------------------------------

void rhi_renderer::draw_line(point2d start, point2d end)
{
    if (rectangle_off_screen({start, end}))
        return;

    if (current_coordinate_system == WORLD) {
        rectangle clip = get_visible_world();
        if (!clip_line_world(clip, start, end))
            return;
        start = m_transform(start);
        end   = m_transform(end);
    }

    // Sub-pixel LOD: discard if screen length < 0.4 px.
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    if (dx * dx + dy * dy < 0.16)
        return;

    m_lines.push_back(make_vertex(start));
    m_lines.push_back(make_vertex(end));
}

// ---- fill_rectangle overrides ----------------------------------------------

void rhi_renderer::fill_rectangle(point2d start, point2d end)
{
    if (rectangle_off_screen({start, end}))
        return;

    point2d s = to_screen(start);
    point2d e = to_screen(end);

    // Normalise so tl.x < br.x and tl.y < br.y (screen y-down).
    point2d tl{ std::min(s.x, e.x), std::min(s.y, e.y) };
    point2d br{ std::max(s.x, e.x), std::max(s.y, e.y) };
    push_fill_rect(tl, br);
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
    if (rectangle_off_screen({start, end}))
        return;

    point2d s = to_screen(start);
    point2d e = to_screen(end);

    point2d tl{ std::min(s.x, e.x), std::min(s.y, e.y) };
    point2d br{ std::max(s.x, e.x), std::max(s.y, e.y) };
    push_draw_rect(tl, br);
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

    m_rhi_widget->set_frame_data(
        std::move(m_lines),
        std::move(m_fill_verts),
        std::move(m_draw_verts),
        compute_mvp(),
        m_overlay,
        m_bg_color);

    m_rhi_widget->update();
}

} // namespace ezgl

#endif // EZGL_QT && EZGL_RHI
