#ifdef EZGL_RHI

#include "ezgl/qt/rhi_renderer.hpp"
#include "ezgl/camera.hpp"
#include "ezgl/logutils.hpp"
#include <functional>

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

constexpr double kPolygonEpsilon = 1e-12;

std::uint32_t pack_color_rgba(const ezgl::color& c)
{
    return std::uint32_t(c.red)
         | (std::uint32_t(c.green) << 8)
         | (std::uint32_t(c.blue)  << 16)
         | (std::uint32_t(c.alpha) << 24);
}

struct Triangle {
    ezgl::point2d a;
    ezgl::point2d b;
    ezgl::point2d c;
};

double signed_twice_area(const std::vector<ezgl::point2d>& polygon)
{
    double area = 0.0;
    for (std::size_t i = 0; i < polygon.size(); ++i) {
        const ezgl::point2d& p = polygon[i];
        const ezgl::point2d& q = polygon[(i + 1) % polygon.size()];
        area += p.x * q.y - q.x * p.y;
    }
    return area;
}

double cross(const ezgl::point2d& a,
             const ezgl::point2d& b,
             const ezgl::point2d& c)
{
    return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
}

bool point_in_triangle(const ezgl::point2d& p,
                       const ezgl::point2d& a,
                       const ezgl::point2d& b,
                       const ezgl::point2d& c)
{
    const double c0 = cross(a, b, p);
    const double c1 = cross(b, c, p);
    const double c2 = cross(c, a, p);
    const bool non_negative =
        c0 >= -kPolygonEpsilon && c1 >= -kPolygonEpsilon && c2 >= -kPolygonEpsilon;
    const bool non_positive =
        c0 <= kPolygonEpsilon && c1 <= kPolygonEpsilon && c2 <= kPolygonEpsilon;
    return non_negative || non_positive;
}

std::vector<ezgl::point2d> normalized_polygon_points(const std::vector<ezgl::point2d>& input)
{
    std::vector<ezgl::point2d> polygon;
    polygon.reserve(input.size());
    for (const ezgl::point2d& point : input) {
        if (!polygon.empty() && polygon.back() == point)
            continue;
        polygon.push_back(point);
    }

    if (polygon.size() > 1 && polygon.front() == polygon.back())
        polygon.pop_back();

    return polygon;
}

std::vector<Triangle> triangulate_simple_polygon(const std::vector<ezgl::point2d>& input)
{
    const std::vector<ezgl::point2d> polygon = normalized_polygon_points(input);
    if (polygon.size() < 3)
        return {};

    const double area = signed_twice_area(polygon);
    if (std::abs(area) <= kPolygonEpsilon)
        return {};

    const bool ccw = area > 0.0;
    std::vector<std::size_t> remaining;
    remaining.reserve(polygon.size());
    for (std::size_t i = 0; i < polygon.size(); ++i)
        remaining.push_back(i);

    std::vector<Triangle> triangles;
    triangles.reserve(polygon.size() - 2);

    std::size_t guard = 0;
    const std::size_t max_guard = polygon.size() * polygon.size();
    while (remaining.size() > 3 && guard < max_guard) {
        bool ear_found = false;
        for (std::size_t i = 0; i < remaining.size(); ++i) {
            const std::size_t prev = remaining[(i + remaining.size() - 1) % remaining.size()];
            const std::size_t curr = remaining[i];
            const std::size_t next = remaining[(i + 1) % remaining.size()];

            const double corner_cross = cross(polygon[prev], polygon[curr], polygon[next]);
            if ((ccw && corner_cross <= kPolygonEpsilon)
                || (!ccw && corner_cross >= -kPolygonEpsilon)) {
                continue;
            }

            bool contains_other_vertex = false;
            for (std::size_t j = 0; j < remaining.size(); ++j) {
                const std::size_t candidate = remaining[j];
                if (candidate == prev || candidate == curr || candidate == next)
                    continue;
                if (point_in_triangle(polygon[candidate],
                                      polygon[prev],
                                      polygon[curr],
                                      polygon[next])) {
                    contains_other_vertex = true;
                    break;
                }
            }
            if (contains_other_vertex)
                continue;

            triangles.push_back({polygon[prev], polygon[curr], polygon[next]});
            remaining.erase(remaining.begin() + std::ptrdiff_t(i));
            ear_found = true;
            break;
        }

        if (!ear_found)
            break;
        ++guard;
    }

    if (remaining.size() == 3) {
        triangles.push_back({
            polygon[remaining[0]],
            polygon[remaining[1]],
            polygon[remaining[2]]
        });
    }

    if (triangles.size() != polygon.size() - 2)
        return {};

    return triangles;
}

ezgl::point2d intersect_vertical(const ezgl::point2d& a,
                                 const ezgl::point2d& b,
                                 double                x)
{
    const double dx = b.x - a.x;
    if (std::abs(dx) <= kPolygonEpsilon)
        return {x, a.y};

    const double t = (x - a.x) / dx;
    return {x, a.y + t * (b.y - a.y)};
}

ezgl::point2d intersect_horizontal(const ezgl::point2d& a,
                                   const ezgl::point2d& b,
                                   double                y)
{
    const double dy = b.y - a.y;
    if (std::abs(dy) <= kPolygonEpsilon)
        return {a.x, y};

    const double t = (y - a.y) / dy;
    return {a.x + t * (b.x - a.x), y};
}

template<typename InsideFn, typename IntersectFn>
std::vector<ezgl::point2d> clip_polygon_edge(const std::vector<ezgl::point2d>& polygon,
                                             InsideFn                           inside,
                                             IntersectFn                        intersect)
{
    std::vector<ezgl::point2d> out;
    if (polygon.empty())
        return out;

    ezgl::point2d previous = polygon.back();
    bool previous_inside = inside(previous);
    for (const ezgl::point2d& current : polygon) {
        const bool current_inside = inside(current);
        if (current_inside != previous_inside)
            out.push_back(intersect(previous, current));
        if (current_inside)
            out.push_back(current);

        previous = current;
        previous_inside = current_inside;
    }

    return out;
}

std::vector<ezgl::point2d> clip_convex_polygon_to_rect(const std::vector<ezgl::point2d>& polygon,
                                                       const ezgl::rectangle&             clip)
{
    std::vector<ezgl::point2d> clipped = polygon;
    clipped = clip_polygon_edge(
        clipped,
        [&](const ezgl::point2d& p) { return p.x >= clip.left() - kPolygonEpsilon; },
        [&](const ezgl::point2d& a, const ezgl::point2d& b) { return intersect_vertical(a, b, clip.left()); });
    clipped = clip_polygon_edge(
        clipped,
        [&](const ezgl::point2d& p) { return p.x <= clip.right() + kPolygonEpsilon; },
        [&](const ezgl::point2d& a, const ezgl::point2d& b) { return intersect_vertical(a, b, clip.right()); });
    clipped = clip_polygon_edge(
        clipped,
        [&](const ezgl::point2d& p) { return p.y >= clip.bottom() - kPolygonEpsilon; },
        [&](const ezgl::point2d& a, const ezgl::point2d& b) { return intersect_horizontal(a, b, clip.bottom()); });
    clipped = clip_polygon_edge(
        clipped,
        [&](const ezgl::point2d& p) { return p.y <= clip.top() + kPolygonEpsilon; },
        [&](const ezgl::point2d& a, const ezgl::point2d& b) { return intersect_horizontal(a, b, clip.top()); });

    return normalized_polygon_points(clipped);
}

} // namespace

namespace ezgl {

// ---- construction ----------------------------------------------------------

rhi_renderer::rhi_renderer(RhiCanvasWidget* widget,
                             transform_fn     transform,
                             camera*          cam,
                             draw_callback_fn draw_callback,
                             QColor           bg_color)
    : RendererBase(nullptr,    // painter is wired up below via update_painter()
                   transform,  // copy — m_overlay_deferred also needs it
                   cam,
                   nullptr)    // surface not used in RHI path
    , m_rhi_widget(widget)
    , m_bg_color(bg_color)
    , m_overlay(std::max(1, widget->width()),
                std::max(1, widget->height()),
                QImage::Format_ARGB32_Premultiplied)
    , m_overlay_painter(&m_overlay)
    , m_overlay_deferred(std::make_unique<deferred_renderer>(
          &m_overlay_painter,
          std::move(transform),   // move the second copy into overlay renderer
          cam,
          &m_overlay))
{
    (void)draw_callback;
    ensure_tile_grid();
    clear_tile_geometry();
    m_overlay_deferred->clear_overlay_and_batches();
    m_overlay.fill(Qt::transparent);
    update_painter(&m_overlay_painter, &m_overlay);
    m_overlay_painter.setAntialias(false);
    m_overlay_painter.setSmoothPixmap(false);
}

// ---- irenderer: coordinate system / viewport ------------------------------

void rhi_renderer::set_coordinate_system(t_coordinate_system cs)
{
    do_set_coordinate_system(cs);
    m_overlay_deferred->set_coordinate_system(cs);
}

void rhi_renderer::set_visible_world(rectangle new_world)
{
    do_set_visible_world(new_world);
    m_overlay_deferred->set_visible_world(new_world);
}

rectangle rhi_renderer::get_visible_world()
{
    return get_visible_world_impl();
}

rectangle rhi_renderer::get_visible_screen() const
{
    return get_visible_screen_impl();
}

rectangle rhi_renderer::world_to_screen(const rectangle& box)
{
    return world_to_screen_impl(box);
}

// ---- irenderer: state setters ---------------------------------------------

void rhi_renderer::set_color(color c)
{
    do_set_color(c);
    m_overlay_deferred->set_color(c);
}

void rhi_renderer::set_color(color c, uint_fast8_t alpha)
{
    do_set_color(c, alpha);
    m_overlay_deferred->set_color(c, alpha);
}

void rhi_renderer::set_color(uint_fast8_t r, uint_fast8_t g,
                              uint_fast8_t b, uint_fast8_t a)
{
    do_set_color(r, g, b, a);
    m_overlay_deferred->set_color(r, g, b, a);
}

void rhi_renderer::set_line_cap(line_cap cap)
{
    do_set_line_cap(cap);
    m_overlay_deferred->set_line_cap(cap);
}

void rhi_renderer::set_line_dash(line_dash dash)
{
    do_set_line_dash(dash);
    m_overlay_deferred->set_line_dash(dash);
}

void rhi_renderer::set_line_width(int width)
{
    do_set_line_width(width);
    m_overlay_deferred->set_line_width(width);
}

void rhi_renderer::set_font_size(double size)
{
    do_set_font_size(size);
    m_overlay_deferred->set_font_size(size);
}

void rhi_renderer::format_font(std::string const& family,
                                font_slant slant, font_weight weight)
{
    do_format_font(family, slant, weight);
    m_overlay_deferred->format_font(family, slant, weight);
}

void rhi_renderer::format_font(std::string const& family,
                                font_slant slant, font_weight weight,
                                double new_size)
{
    do_set_font_size(new_size);
    do_format_font(family, slant, weight);
    m_overlay_deferred->format_font(family, slant, weight, new_size);
}

void rhi_renderer::set_text_rotation(double degrees)
{
    do_set_text_rotation(degrees);
    m_overlay_deferred->set_text_rotation(degrees);
}

void rhi_renderer::set_horiz_justification(justification j)
{
    do_set_horiz_justification(j);
    m_overlay_deferred->set_horiz_justification(j);
}

void rhi_renderer::set_vert_justification(justification j)
{
    do_set_vert_justification(j);
    m_overlay_deferred->set_vert_justification(j);
}

// ---- irenderer: overlay draw calls ----------------------------------------

void rhi_renderer::fill_poly(std::vector<point2d> const& points)
{
    if (current_coordinate_system != WORLD) {
        m_overlay_deferred->fill_poly(points);
        return;
    }
    if (m_skip_tile_writes)
        return;

    if (points.size() < 3)
        return;

    double x_min = points[0].x, x_max = points[0].x;
    double y_min = points[0].y, y_max = points[0].y;
    for (std::size_t i = 1; i < points.size(); ++i) {
        x_min = std::min(x_min, points[i].x);
        x_max = std::max(x_max, points[i].x);
        y_min = std::min(y_min, points[i].y);
        y_max = std::max(y_max, points[i].y);
    }

    const std::vector<Triangle> triangles = triangulate_simple_polygon(points);
    if (triangles.empty()) {
        qWarning("rhi_renderer: failed to triangulate polygon with %llu points",
                 static_cast<unsigned long long>(points.size()));
        return;
    }

    const StyleKey style_key = current_style_key(PrimitiveType::FilledPoly);
    const std::uint32_t rgba = current_packed_color();
    for (const Triangle& triangle : triangles) {
        append_fill_triangle_to_tiles(triangle.a, triangle.b, triangle.c, style_key, rgba);
    }
}

void rhi_renderer::draw_elliptic_arc(point2d center, double radius_x, double radius_y,
                                      double start_angle, double extent_angle)
{
    m_overlay_deferred->draw_elliptic_arc(center, radius_x, radius_y,
                                          start_angle, extent_angle);
}

void rhi_renderer::draw_arc(point2d center, double radius,
                             double start_angle, double extent_angle)
{
    m_overlay_deferred->draw_arc(center, radius, start_angle, extent_angle);
}

void rhi_renderer::fill_elliptic_arc(point2d center, double radius_x, double radius_y,
                                      double start_angle, double extent_angle)
{
    m_overlay_deferred->fill_elliptic_arc(center, radius_x, radius_y,
                                          start_angle, extent_angle);
}

void rhi_renderer::fill_arc(point2d center, double radius,
                             double start_angle, double extent_angle)
{
    m_overlay_deferred->fill_arc(center, radius, start_angle, extent_angle);
}

void rhi_renderer::draw_text(point2d point, std::string const& text)
{
    m_overlay_deferred->draw_text(point, text);
}

void rhi_renderer::draw_text(point2d point, std::string const& text,
                              double bound_x, double bound_y)
{
    m_overlay_deferred->draw_text(point, text, bound_x, bound_y);
}

void rhi_renderer::draw_surface(surface* p_surface, point2d anchor_point,
                                 double scale_factor)
{
    m_overlay_deferred->draw_surface(p_surface, anchor_point, scale_factor);
}

// ---- frame lifecycle -------------------------------------------------------

void rhi_renderer::begin_frame()
{
    ensure_tile_grid();
    clear_tile_geometry();
    m_overlay_deferred->clear_overlay_and_batches();
    m_skip_tile_writes = false;

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
    update_painter(&m_overlay_painter, &m_overlay);
    m_overlay_deferred->set_painter_surface(&m_overlay_painter, &m_overlay);

    // Match the deferred path semantics: each redraw starts from the renderer
    // defaults rather than inheriting state from the previous frame.
    current_coordinate_system = WORLD;
    rotation_angle = 0.0;
    horiz_justification = justification::center;
    vert_justification = justification::center;
    current_color = {0, 0, 0, 255};
    current_line_width = 0;
    current_line_cap = line_cap::butt;
    current_line_dash = line_dash::none;
    set_color(current_color);
    set_line_width(current_line_width);
    set_line_cap(current_line_cap);
    set_line_dash(current_line_dash);
}

void rhi_renderer::begin_overlay_frame()
{
    // End painter if still active from a previous pass.
    if (m_overlay_painter.isActive())
        m_overlay_painter.end();

    const int w = std::max(1, m_rhi_widget->width());
    const int h = std::max(1, m_rhi_widget->height());
    if (m_overlay.width() != w || m_overlay.height() != h)
        m_overlay = QImage(w, h, QImage::Format_ARGB32_Premultiplied);

    m_overlay.fill(Qt::transparent);

    m_overlay_painter.begin(&m_overlay);
    m_overlay_painter.setAntialias(false);
    m_overlay_painter.setSmoothPixmap(false);
    update_painter(&m_overlay_painter, &m_overlay);
    m_overlay_deferred->set_painter_surface(&m_overlay_painter, &m_overlay);

    m_skip_tile_writes = false;
}

void rhi_renderer::render_cached_overlay()
{
    begin_overlay_frame();
    m_overlay_deferred->replay_overlay();

    if (m_overlay_painter.isActive())
        m_overlay_painter.end();
}

// ---- helpers ---------------------------------------------------------------

inline PosVertex rhi_renderer::make_vertex(point2d p) const
{
    return PosVertex{float(p.x), float(p.y)};
}

std::uint32_t rhi_renderer::current_packed_color() const
{
    return pack_color_rgba(current_color);
}

StyleKey rhi_renderer::current_style_key(PrimitiveType primitive_type,
                                         float         line_width_px) const
{
    const int rounded_width = int(std::lround(line_width_px));
    const std::uint16_t packed_width = std::uint16_t(
        std::clamp(rounded_width, 0, 65535));
    const std::uint8_t packed_dash = primitive_type == PrimitiveType::DashedLine
        ? std::uint8_t(current_line_dash)
        : 0;
    return pack_style_key(primitive_type,
                          current_packed_color(),
                          packed_width,
                          packed_dash);
}

static bool matches_style_key(StyleKey lhs, StyleKey rhs)
{
    return lhs == rhs;
}

rhi_renderer::TileThinLineBatch& rhi_renderer::ensure_thin_line_batch(RhiTileBatch& tile,
                                                                      StyleKey     style_key,
                                                                      std::uint32_t rgba)
{
    auto it = std::find_if(tile.thin_line_batches.begin(),
                           tile.thin_line_batches.end(),
                           [style_key](const TileThinLineBatch& batch) {
                               return matches_style_key(batch.style_key, style_key);
                           });
    if (it != tile.thin_line_batches.end())
        return *it;

    tile.thin_line_batches.push_back(TileThinLineBatch{style_key, rgba, {}});
    return tile.thin_line_batches.back();
}

rhi_renderer::TileFillRectBatch& rhi_renderer::ensure_fill_rect_batch(RhiTileBatch& tile,
                                                                      StyleKey     style_key,
                                                                      std::uint32_t rgba)
{
    auto it = std::find_if(tile.fill_rect_batches.begin(),
                           tile.fill_rect_batches.end(),
                           [style_key](const TileFillRectBatch& batch) {
                               return matches_style_key(batch.style_key, style_key);
                           });
    if (it != tile.fill_rect_batches.end())
        return *it;

    tile.fill_rect_batches.push_back(TileFillRectBatch{style_key, rgba, {}});
    return tile.fill_rect_batches.back();
}

rhi_renderer::TileFillPolyBatch& rhi_renderer::ensure_fill_poly_batch(RhiTileBatch& tile,
                                                                      StyleKey     style_key,
                                                                      std::uint32_t rgba)
{
    auto it = std::find_if(tile.fill_poly_batches.begin(),
                           tile.fill_poly_batches.end(),
                           [style_key](const TileFillPolyBatch& batch) {
                               return matches_style_key(batch.style_key, style_key);
                           });
    if (it != tile.fill_poly_batches.end())
        return *it;

    tile.fill_poly_batches.push_back(TileFillPolyBatch{style_key, rgba, {}});
    return tile.fill_poly_batches.back();
}

rhi_renderer::TileThickLineBatch& rhi_renderer::ensure_thick_line_batch(RhiTileBatch& tile,
                                                                        StyleKey     style_key,
                                                                        std::uint32_t rgba)
{
    auto it = std::find_if(tile.thick_line_batches.begin(),
                           tile.thick_line_batches.end(),
                           [style_key](const TileThickLineBatch& batch) {
                               return matches_style_key(batch.style_key, style_key);
                           });
    if (it != tile.thick_line_batches.end())
        return *it;

    tile.thick_line_batches.push_back(TileThickLineBatch{style_key, rgba, {}});
    return tile.thick_line_batches.back();
}

rhi_renderer::TileDashedLineBatch& rhi_renderer::ensure_dashed_line_batch(RhiTileBatch& tile,
                                                                          StyleKey     style_key,
                                                                          std::uint32_t rgba)
{
    auto it = std::find_if(tile.dashed_line_batches.begin(),
                           tile.dashed_line_batches.end(),
                           [style_key](const TileDashedLineBatch& batch) {
                               return matches_style_key(batch.style_key, style_key);
                           });
    if (it != tile.dashed_line_batches.end())
        return *it;

    tile.dashed_line_batches.push_back(TileDashedLineBatch{style_key, rgba, {}});
    return tile.dashed_line_batches.back();
}

void rhi_renderer::append_thin_line_segment(RhiTileBatch& tile,
                                            point2d       start,
                                            point2d       end,
                                            StyleKey      style_key,
                                            std::uint32_t rgba)
{
    TileThinLineBatch& batch = ensure_thin_line_batch(tile, style_key, rgba);
    batch.verts.push_back(make_vertex(start));
    batch.verts.push_back(make_vertex(end));
}

void rhi_renderer::append_fill_rect(RhiTileBatch& tile,
                                    point2d       p0,
                                    point2d       p1,
                                    StyleKey      style_key,
                                    std::uint32_t rgba)
{
    if (p1.x <= p0.x || p1.y <= p0.y)
        return;

    TileFillRectBatch& batch = ensure_fill_rect_batch(tile, style_key, rgba);
    batch.instances.push_back(FillRectInstance{
        float(p0.x), float(p0.y),
        float(p1.x), float(p1.y)
    });
}

void rhi_renderer::append_fill_triangle(RhiTileBatch& tile,
                                        point2d       a,
                                        point2d       b,
                                        point2d       c,
                                        StyleKey      style_key,
                                        std::uint32_t rgba)
{
    if (std::abs(cross(a, b, c)) <= kPolygonEpsilon)
        return;

    TileFillPolyBatch& batch = ensure_fill_poly_batch(tile, style_key, rgba);
    batch.verts.push_back(make_vertex(a));
    batch.verts.push_back(make_vertex(b));
    batch.verts.push_back(make_vertex(c));
}

void rhi_renderer::ensure_tile_grid()
{
    const rectangle scene = m_camera->get_initial_world();
    const double scene_width = std::max(scene.width(), std::numeric_limits<double>::epsilon());
    const double scene_height = std::max(scene.height(), std::numeric_limits<double>::epsilon());
    const rectangle normalized_scene{{scene.left(), scene.bottom()}, scene_width, scene_height};

    if (m_tiles.size() == std::size_t(kTileGridDimension * kTileGridDimension)
        && normalized_scene == m_scene_bounds) {
        return;
    }

    m_scene_bounds = normalized_scene;
    m_tile_width = m_scene_bounds.width() / double(kTileGridDimension);
    m_tile_height = m_scene_bounds.height() / double(kTileGridDimension);
    m_tiles.clear();
    m_tiles.resize(std::size_t(kTileGridDimension * kTileGridDimension));

    for (int ty = 0; ty < kTileGridDimension; ++ty) {
        const double bottom = m_scene_bounds.bottom() + double(ty) * m_tile_height;
        const double top = (ty + 1 == kTileGridDimension)
            ? m_scene_bounds.top()
            : (bottom + m_tile_height);

        for (int tx = 0; tx < kTileGridDimension; ++tx) {
            const double left = m_scene_bounds.left() + double(tx) * m_tile_width;
            const double right = (tx + 1 == kTileGridDimension)
                ? m_scene_bounds.right()
                : (left + m_tile_width);
            RhiTileBatch& tile = m_tiles[std::size_t(tile_index(tx, ty))];
            tile.world_bounds = rectangle{{left, bottom}, {right, top}};
            tile.tile_x = std::uint16_t(tx);
            tile.tile_y = std::uint16_t(ty);
        }
    }
}

void rhi_renderer::clear_tile_geometry()
{
    for (RhiTileBatch& tile : m_tiles) {
        tile.thin_line_batches.clear();
        tile.fill_rect_batches.clear();
        tile.fill_poly_batches.clear();
        tile.thick_line_batches.clear();
        tile.dashed_line_batches.clear();
    }
}

int rhi_renderer::clamp_tile_x(double x) const
{
    const double normalized = (x - m_scene_bounds.left()) / m_tile_width;
    return std::clamp(int(std::floor(normalized)), 0, kTileGridDimension - 1);
}

int rhi_renderer::clamp_tile_y(double y) const
{
    const double normalized = (y - m_scene_bounds.bottom()) / m_tile_height;
    return std::clamp(int(std::floor(normalized)), 0, kTileGridDimension - 1);
}

int rhi_renderer::tile_index(int tile_x, int tile_y) const
{
    return tile_y * kTileGridDimension + tile_x;
}

rhi_renderer::RhiTileBatch& rhi_renderer::tile_at(int tile_x, int tile_y)
{
    return m_tiles[std::size_t(tile_index(tile_x, tile_y))];
}

void rhi_renderer::append_line_to_tiles(point2d start,
                                        point2d end,
                                        StyleKey style_key,
                                        std::uint32_t rgba)
{
    const rectangle bounds{start, end};
    const int min_tx = clamp_tile_x(bounds.left());
    const int max_tx = clamp_tile_x(bounds.right());
    const int min_ty = clamp_tile_y(bounds.bottom());
    const int max_ty = clamp_tile_y(bounds.top());

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            point2d clipped_start = start;
            point2d clipped_end = end;
            RhiTileBatch& tile = tile_at(tx, ty);
            if (!clip_line_world(tile.world_bounds, clipped_start, clipped_end))
                continue;
            append_thin_line_segment(tile,
                                     clipped_start,
                                     clipped_end,
                                     style_key,
                                     rgba);
        }
    }
}

void rhi_renderer::append_fill_rect_to_tiles(point2d p0,
                                             point2d p1,
                                             StyleKey style_key,
                                             std::uint32_t rgba)
{
    const rectangle bounds{p0, p1};
    const int min_tx = clamp_tile_x(bounds.left());
    const int max_tx = clamp_tile_x(bounds.right());
    const int min_ty = clamp_tile_y(bounds.bottom());
    const int max_ty = clamp_tile_y(bounds.top());

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            RhiTileBatch& tile = tile_at(tx, ty);
            const double left = std::max(bounds.left(), tile.world_bounds.left());
            const double right = std::min(bounds.right(), tile.world_bounds.right());
            const double bottom = std::max(bounds.bottom(), tile.world_bounds.bottom());
            const double top = std::min(bounds.top(), tile.world_bounds.top());
            append_fill_rect(tile,
                             {left, bottom},
                             {right, top},
                             style_key,
                             rgba);
        }
    }
}

void rhi_renderer::append_fill_triangle_to_tiles(point2d    a,
                                                 point2d    b,
                                                 point2d    c,
                                                 StyleKey   style_key,
                                                 std::uint32_t rgba)
{
    const double x_min = std::min({a.x, b.x, c.x});
    const double x_max = std::max({a.x, b.x, c.x});
    const double y_min = std::min({a.y, b.y, c.y});
    const double y_max = std::max({a.y, b.y, c.y});
    const rectangle bounds{{x_min, y_min}, {x_max, y_max}};
    const int min_tx = clamp_tile_x(bounds.left());
    const int max_tx = clamp_tile_x(bounds.right());
    const int min_ty = clamp_tile_y(bounds.bottom());
    const int max_ty = clamp_tile_y(bounds.top());
    const std::vector<point2d> triangle = {a, b, c};

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            RhiTileBatch& tile = tile_at(tx, ty);
            const std::vector<point2d> clipped =
                clip_convex_polygon_to_rect(triangle, tile.world_bounds);
            if (clipped.size() < 3)
                continue;

            const point2d anchor = clipped.front();
            for (std::size_t i = 1; i + 1 < clipped.size(); ++i) {
                append_fill_triangle(tile, anchor, clipped[i], clipped[i + 1], style_key, rgba);
            }
        }
    }
}

// ---- thick line helpers ----------------------------------------------------

void rhi_renderer::append_thick_segment(RhiTileBatch& tile,
                                        point2d       start,
                                        point2d       end,
                                        StyleKey      style_key,
                                        std::uint32_t rgba)
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    if (std::sqrt(dx * dx + dy * dy) < 1e-10)
        return; // degenerate (zero-length) segment

    // One instance record (16 bytes) per segment.
    // The vertex shader reconstructs all 4 quad corners from this record plus
    // the constant 4-corner quad buffer — no per-vertex duplication of endpoints.
    TileThickLineBatch& batch = ensure_thick_line_batch(tile, style_key, rgba);
    batch.instances.push_back({
        float(start.x), float(start.y),
        float(end.x),   float(end.y)
    });
}

void rhi_renderer::append_thick_line_to_tiles(point2d    start,
                                              point2d    end,
                                              StyleKey   style_key,
                                              std::uint32_t rgba)
{
    const rectangle bounds{start, end};
    const int min_tx = clamp_tile_x(bounds.left());
    const int max_tx = clamp_tile_x(bounds.right());
    const int min_ty = clamp_tile_y(bounds.bottom());
    const int max_ty = clamp_tile_y(bounds.top());

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            point2d clipped_start = start;
            point2d clipped_end   = end;
            RhiTileBatch& tile = tile_at(tx, ty);
            if (!clip_line_world(tile.world_bounds, clipped_start, clipped_end))
                continue;
            append_thick_segment(tile,
                                 clipped_start,
                                 clipped_end,
                                 style_key,
                                 rgba);
        }
    }
}

void rhi_renderer::append_thick_draw_segment_to_tiles(point2d    start,
                                                      point2d    end,
                                                      StyleKey   style_key,
                                                      std::uint32_t rgba)
{
    // Reuses the same geometry pool as thick draw_lines (same pipeline).
    append_thick_line_to_tiles(start, end, style_key, rgba);
}

// ---- dashed line helpers ---------------------------------------------------

void rhi_renderer::append_dashed_segment(RhiTileBatch& tile,
                                         point2d       start,
                                         point2d       end,
                                         float         phase_world,
                                         StyleKey      style_key,
                                         std::uint32_t rgba)
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    if (std::sqrt(dx * dx + dy * dy) < 1e-10)
        return;

    TileDashedLineBatch& batch = ensure_dashed_line_batch(tile, style_key, rgba);
    batch.instances.push_back({
        float(start.x), float(start.y),
        float(end.x),   float(end.y),
        phase_world
    });
}

void rhi_renderer::append_dashed_line_to_tiles(point2d    start,
                                               point2d    end,
                                               StyleKey   style_key,
                                               std::uint32_t rgba)
{
    const point2d original_start = start;
    const rectangle bounds{start, end};
    const int min_tx = clamp_tile_x(bounds.left());
    const int max_tx = clamp_tile_x(bounds.right());
    const int min_ty = clamp_tile_y(bounds.bottom());
    const int max_ty = clamp_tile_y(bounds.top());

    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            point2d clipped_start = start;
            point2d clipped_end   = end;
            RhiTileBatch& tile = tile_at(tx, ty);
            if (!clip_line_world(tile.world_bounds, clipped_start, clipped_end))
                continue;
            const double phase_dx = clipped_start.x - original_start.x;
            const double phase_dy = clipped_start.y - original_start.y;
            const float phase_world = float(std::sqrt(phase_dx * phase_dx
                                                      + phase_dy * phase_dy));
            append_dashed_segment(tile, clipped_start, clipped_end,
                                  phase_world, style_key, rgba);
        }
    }
}

void rhi_renderer::append_dashed_draw_segment_to_tiles(point2d    start,
                                                       point2d    end,
                                                       StyleKey   style_key,
                                                       std::uint32_t rgba)
{
    append_dashed_line_to_tiles(start, end, style_key, rgba);
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

void rhi_renderer::draw_line(point2d start, point2d end)
{
    if (current_coordinate_system != WORLD) {
        // SCREEN mode goes to the cached overlay replay path.
        m_overlay_deferred->draw_line(start, end);
        return;
    }
    if (m_skip_tile_writes)
        return;

    const std::uint32_t rgba = current_packed_color();

    if (current_line_dash != line_dash::none) {
        const float w = float(std::max(1, current_line_width));
        append_dashed_line_to_tiles(start, end,
                                    current_style_key(PrimitiveType::DashedLine, w),
                                    rgba);
        return;
    }

    if (current_line_width > 1) {
        const float w = float(current_line_width);
        append_thick_line_to_tiles(start, end,
                                   current_style_key(PrimitiveType::ThickLine, w),
                                   rgba);
        return;
    }

    append_line_to_tiles(start, end, current_style_key(PrimitiveType::ThinLine), rgba);
}

// ---- fill_rectangle overrides ----------------------------------------------

void rhi_renderer::fill_rectangle(point2d start, point2d end)
{
    if (current_coordinate_system != WORLD) {
        m_overlay_deferred->fill_rectangle(start, end);
        return;
    }
    if (m_skip_tile_writes)
        return;

    const point2d p0{ std::min(start.x, end.x), std::min(start.y, end.y) };
    const point2d p1{ std::max(start.x, end.x), std::max(start.y, end.y) };
    const std::uint32_t rgba = current_packed_color();
    append_fill_rect_to_tiles(p0, p1, current_style_key(PrimitiveType::FilledRect), rgba);
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
        m_overlay_deferred->draw_rectangle(start, end);
        return;
    }
    if (m_skip_tile_writes)
        return;

    const point2d p0{ std::min(start.x, end.x), std::min(start.y, end.y) };
    const point2d p1{ std::max(start.x, end.x), std::max(start.y, end.y) };
    const std::uint32_t rgba = current_packed_color();

    if (current_line_dash != line_dash::none) {
        const float w = float(std::max(1, current_line_width));
        const StyleKey style_key = current_style_key(PrimitiveType::DashedLine, w);
        append_dashed_draw_segment_to_tiles({p0.x, p0.y}, {p1.x, p0.y}, style_key, rgba);
        append_dashed_draw_segment_to_tiles({p1.x, p0.y}, {p1.x, p1.y}, style_key, rgba);
        append_dashed_draw_segment_to_tiles({p1.x, p1.y}, {p0.x, p1.y}, style_key, rgba);
        append_dashed_draw_segment_to_tiles({p0.x, p1.y}, {p0.x, p0.y}, style_key, rgba);
        return;
    }

    if (current_line_width > 1) {
        const float w = float(current_line_width);
        const StyleKey style_key = current_style_key(PrimitiveType::ThickLine, w);
        append_thick_draw_segment_to_tiles({p0.x, p0.y}, {p1.x, p0.y}, style_key, rgba);
        append_thick_draw_segment_to_tiles({p1.x, p0.y}, {p1.x, p1.y}, style_key, rgba);
        append_thick_draw_segment_to_tiles({p1.x, p1.y}, {p0.x, p1.y}, style_key, rgba);
        append_thick_draw_segment_to_tiles({p0.x, p1.y}, {p0.x, p0.y}, style_key, rgba);
        return;
    }

    const StyleKey style_key = current_style_key(PrimitiveType::ThinLine);
    append_line_to_tiles({p0.x, p0.y}, {p1.x, p0.y}, style_key, rgba);
    append_line_to_tiles({p1.x, p0.y}, {p1.x, p1.y}, style_key, rgba);
    append_line_to_tiles({p1.x, p1.y}, {p0.x, p1.y}, style_key, rgba);
    append_line_to_tiles({p0.x, p1.y}, {p0.x, p0.y}, style_key, rgba);
}

void rhi_renderer::draw_rectangle(point2d start, double width, double height)
{
    draw_rectangle(start, {start.x + width, start.y + height});
}

void rhi_renderer::draw_rectangle(rectangle r)
{
    draw_rectangle({r.left(), r.bottom()}, {r.right(), r.top()});
}

SceneBuffers rhi_renderer::build_scene_buffers() const
{
    SceneBuffers scene;

    for (const RhiTileBatch& tile : m_tiles) {
        if (tile.empty())
            continue;

        for (const TileThinLineBatch& batch : tile.thin_line_batches) {
            if (batch.verts.empty())
                continue;
            ThinLineStyleBuffer& scene_buffer = scene.thin_lines[batch.style_key];
            if (scene_buffer.chunks.empty()) {
                scene_buffer.style_key = batch.style_key;
                scene_buffer.rgba = batch.rgba;
            }
            const std::size_t offset = scene_buffer.verts.size();
            scene_buffer.chunks.push_back(Chunk{
                tile.world_bounds,
                std::uint32_t(offset),
                std::uint32_t(batch.verts.size())
            });
            scene_buffer.verts.insert(scene_buffer.verts.end(),
                                      batch.verts.begin(),
                                      batch.verts.end());
        }

        for (const TileFillRectBatch& batch : tile.fill_rect_batches) {
            if (batch.instances.empty())
                continue;
            FillRectStyleBuffer& scene_buffer = scene.fill_rects[batch.style_key];
            if (scene_buffer.chunks.empty()) {
                scene_buffer.style_key = batch.style_key;
                scene_buffer.rgba = batch.rgba;
            }
            const std::size_t offset = scene_buffer.instances.size();
            scene_buffer.chunks.push_back(Chunk{
                tile.world_bounds,
                std::uint32_t(offset),
                std::uint32_t(batch.instances.size())
            });
            scene_buffer.instances.insert(scene_buffer.instances.end(),
                                          batch.instances.begin(),
                                          batch.instances.end());
        }

        for (const TileFillPolyBatch& batch : tile.fill_poly_batches) {
            if (batch.verts.empty())
                continue;
            FillPolyStyleBuffer& scene_buffer = scene.fill_polys[batch.style_key];
            if (scene_buffer.chunks.empty()) {
                scene_buffer.style_key = batch.style_key;
                scene_buffer.rgba = batch.rgba;
            }
            const std::size_t offset = scene_buffer.verts.size();
            scene_buffer.chunks.push_back(Chunk{
                tile.world_bounds,
                std::uint32_t(offset),
                std::uint32_t(batch.verts.size())
            });
            scene_buffer.verts.insert(scene_buffer.verts.end(),
                                      batch.verts.begin(),
                                      batch.verts.end());
        }

        for (const TileThickLineBatch& batch : tile.thick_line_batches) {
            if (batch.instances.empty())
                continue;
            ThickLineStyleBuffer& scene_buffer = scene.thick_lines[batch.style_key];
            if (scene_buffer.chunks.empty()) {
                scene_buffer.style_key = batch.style_key;
                scene_buffer.rgba = batch.rgba;
            }
            const std::size_t offset = scene_buffer.instances.size();
            scene_buffer.chunks.push_back(Chunk{
                tile.world_bounds,
                std::uint32_t(offset),
                std::uint32_t(batch.instances.size())
            });
            scene_buffer.instances.insert(scene_buffer.instances.end(),
                                          batch.instances.begin(),
                                          batch.instances.end());
        }

        for (const TileDashedLineBatch& batch : tile.dashed_line_batches) {
            if (batch.instances.empty())
                continue;
            DashedLineStyleBuffer& scene_buffer = scene.dashed_lines[batch.style_key];
            if (scene_buffer.chunks.empty()) {
                scene_buffer.style_key = batch.style_key;
                scene_buffer.rgba = batch.rgba;
            }
            const std::size_t offset = scene_buffer.instances.size();
            scene_buffer.chunks.push_back(Chunk{
                tile.world_bounds,
                std::uint32_t(offset),
                std::uint32_t(batch.instances.size())
            });
            scene_buffer.instances.insert(scene_buffer.instances.end(),
                                          batch.instances.begin(),
                                          batch.instances.end());
        }
    }

    return scene;
}

// ---- flush -----------------------------------------------------------------

void rhi_renderer::flush()
{
    render_cached_overlay();

    constexpr double kBytesPerMb = 1024.0 * 1024.0;
    SceneBuffers scene_buffers = build_scene_buffers();

#ifdef EZGL_RENDERER_DEBUG
    double line_verts_mb          = 0.0;
    double fill_rect_instances_mb = 0.0;
    double fill_poly_verts_mb     = 0.0;
    double thick_instances_mb     = 0.0;
    double dashed_instances_mb    = 0.0;
    double style_uniforms_mb      = 0.0;

    for (const auto& [style_key, buffer] : scene_buffers.thin_lines) {
        (void)style_key;
        line_verts_mb += double(buffer.verts.size() * sizeof(PosVertex)) / kBytesPerMb;
        style_uniforms_mb += 32.0 / kBytesPerMb;
    }
    for (const auto& [style_key, buffer] : scene_buffers.fill_rects) {
        (void)style_key;
        fill_rect_instances_mb += double(buffer.instances.size() * sizeof(FillRectInstance)) / kBytesPerMb;
        style_uniforms_mb += 32.0 / kBytesPerMb;
    }
    for (const auto& [style_key, buffer] : scene_buffers.fill_polys) {
        (void)style_key;
        fill_poly_verts_mb += double(buffer.verts.size() * sizeof(PosVertex)) / kBytesPerMb;
        style_uniforms_mb += 32.0 / kBytesPerMb;
    }
    for (const auto& [style_key, buffer] : scene_buffers.thick_lines) {
        (void)style_key;
        thick_instances_mb += double(buffer.instances.size() * sizeof(ThickLineInstance)) / kBytesPerMb;
        style_uniforms_mb += 32.0 / kBytesPerMb;
    }
    for (const auto& [style_key, buffer] : scene_buffers.dashed_lines) {
        (void)style_key;
        dashed_instances_mb += double(buffer.instances.size() * sizeof(DashedLineInstance)) / kBytesPerMb;
        style_uniforms_mb += 32.0 / kBytesPerMb;
    }

    const double total_mb =
        line_verts_mb
        + fill_rect_instances_mb
        + fill_poly_verts_mb
        + thick_instances_mb
        + dashed_instances_mb
        + style_uniforms_mb;

    q_debug_stream()
        << std::fixed << std::setprecision(3)
        << "sending to GPU"
        << " total=" << total_mb << " mb"
        << " line_verts=" << line_verts_mb << " mb"
        << " fill_rect_instances=" << fill_rect_instances_mb << " mb"
        << " fill_poly_verts=" << fill_poly_verts_mb << " mb"
        << " thick_instances=" << thick_instances_mb << " mb"
        << " dashed_instances=" << dashed_instances_mb << " mb"
        << " style_uniforms=" << style_uniforms_mb << " mb";
#endif // EZGL_RENDERER_DEBUG

    m_rhi_widget->set_frame_data(
        std::move(scene_buffers),
        compute_mvp(),
        get_visible_world_impl(),
        m_overlay,
        m_bg_color);

    m_rhi_widget->update();
}

// ---- flush_mvp_only --------------------------------------------------------

void rhi_renderer::flush_mvp_only()
{
    render_cached_overlay();
    m_rhi_widget->set_mvp_and_overlay(compute_mvp(), get_visible_world_impl(), m_overlay);
    m_rhi_widget->update();
}

} // namespace ezgl

#endif // EZGL_RHI
