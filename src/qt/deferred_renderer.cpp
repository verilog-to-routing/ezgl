#include "ezgl/qt/deferred_renderer.hpp"
#include "ezgl/camera.hpp"
#include "ezgl/logutils.hpp"

#include <QPen>
#include <cfloat>
#include <QBrush>
#include <QColor>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>
#include <type_traits>
#include <variant>

namespace ezgl {

// ---- helpers -------------------------------------------------------------

static constexpr double kMinReadableTextSize = 8.0;
static constexpr int kCommandSpatialGridDimension = 256;

struct DeferredVisibleStats {
    std::size_t lines = 0;
    std::size_t filled_rects = 0;
    std::size_t outlined_rects = 0;
    std::size_t filled_polys = 0;
    std::size_t outlined_arcs = 0;
    std::size_t filled_arcs = 0;
    std::size_t texts = 0;
    std::size_t surfaces = 0;

    std::size_t total() const
    {
        return lines
             + filled_rects
             + outlined_rects
             + filled_polys
             + outlined_arcs
             + filled_arcs
             + texts
             + surfaces;
    }
};

static void print_visible_stats(const DeferredVisibleStats& stats)
{
    q_debug_stream()
        << "deferred QPainter visible primitives:"
        << " total=" << stats.total()
        << " lines=" << stats.lines
        << " fill_rects=" << stats.filled_rects
        << " draw_rects=" << stats.outlined_rects
        << " fill_polys=" << stats.filled_polys
        << " draw_arcs=" << stats.outlined_arcs
        << " fill_arcs=" << stats.filled_arcs
        << " text=" << stats.texts
        << " surfaces=" << stats.surfaces;
}

static QColor unpack_color(uint32_t rgba)
{
    return QColor(
        int(rgba & 0xFF),
        int((rgba >> 8)  & 0xFF),
        int((rgba >> 16) & 0xFF),
        int((rgba >> 24) & 0xFF));
}

static QPen make_pen(const LineStyleKey &s)
{
    QPen pen;
    pen.setColor(unpack_color(s.color_rgba));
    pen.setWidth(s.line_width == 0 ? 1 : int(s.line_width));
    pen.setCapStyle(Qt::PenCapStyle(s.line_cap));

    if (s.line_dash != 0) {
        double w = s.line_width > 0 ? double(s.line_width) : 1.0;
        QList<double> pat = {5.0 / w, 3.0 / w};
        pen.setStyle(Qt::CustomDashLine);
        pen.setDashPattern(pat);
    } else {
        pen.setStyle(Qt::SolidLine);
    }
    return pen;
}

// ---- construction --------------------------------------------------------

deferred_renderer::deferred_renderer(Painter *painter,
                                     camera *cam)
    : irenderer(painter, cam)
{}

// ---- spatial index -------------------------------------------------------

void deferred_renderer::ensure_command_index_grid()
{
    const rectangle scene = m_camera->get_initial_world();
    const double scene_width =
        std::max(scene.width(), std::numeric_limits<double>::epsilon());
    const double scene_height =
        std::max(scene.height(), std::numeric_limits<double>::epsilon());
    const rectangle normalized_scene{{scene.left(), scene.bottom()}, scene_width, scene_height};

    if (m_indexed_world_command_buckets.size()
            == std::size_t(kCommandSpatialGridDimension * kCommandSpatialGridDimension)
        && normalized_scene == m_command_index_scene_bounds) {
        return;
    }

    m_command_index_scene_bounds = normalized_scene;
    m_command_index_tile_width =
        m_command_index_scene_bounds.width() / double(kCommandSpatialGridDimension);
    m_command_index_tile_height =
        m_command_index_scene_bounds.height() / double(kCommandSpatialGridDimension);
    m_indexed_world_command_buckets.clear();
    m_indexed_world_command_buckets.resize(
        std::size_t(kCommandSpatialGridDimension * kCommandSpatialGridDimension));
}

int deferred_renderer::clamp_command_tile_x(double x) const
{
    const double normalized =
        (x - m_command_index_scene_bounds.left()) / m_command_index_tile_width;
    return std::clamp(int(std::floor(normalized)), 0, kCommandSpatialGridDimension - 1);
}

int deferred_renderer::clamp_command_tile_y(double y) const
{
    const double normalized =
        (y - m_command_index_scene_bounds.bottom()) / m_command_index_tile_height;
    return std::clamp(int(std::floor(normalized)), 0, kCommandSpatialGridDimension - 1);
}

void deferred_renderer::index_world_command(std::uint32_t command_index,
                                                    rectangle      bounds)
{
    ensure_command_index_grid();

    if (bounds.right() < m_command_index_scene_bounds.left()
        || bounds.left() > m_command_index_scene_bounds.right()
        || bounds.top() < m_command_index_scene_bounds.bottom()
        || bounds.bottom() > m_command_index_scene_bounds.top()) {
        m_unindexed_commands.push_back(command_index);
        return;
    }

    const int min_tx = clamp_command_tile_x(bounds.left());
    const int max_tx = clamp_command_tile_x(bounds.right());
    const int min_ty = clamp_command_tile_y(bounds.bottom());
    const int max_ty = clamp_command_tile_y(bounds.top());
    for (int ty = min_ty; ty <= max_ty; ++ty) {
        for (int tx = min_tx; tx <= max_tx; ++tx) {
            const std::size_t bucket_index =
                std::size_t(ty) * std::size_t(kCommandSpatialGridDimension) + std::size_t(tx);
            m_indexed_world_command_buckets[bucket_index].push_back(command_index);
        }
    }
}

// ---- style key builders --------------------------------------------------

LineStyleKey deferred_renderer::current_line_style() const
{
    LineStyleKey s;
    s.color_rgba = uint32_t(current_color.red)
                 | (uint32_t(current_color.green) << 8)
                 | (uint32_t(current_color.blue)  << 16)
                 | (uint32_t(current_color.alpha) << 24);
    s.line_width = uint16_t(std::clamp(current_line_width, 0, 65535));
    s.line_cap   = uint8_t(current_line_cap);
    s.line_dash  = uint8_t(current_line_dash);
    return s;
}

FillStyleKey deferred_renderer::current_fill_style() const
{
    FillStyleKey s;
    s.color_rgba = uint32_t(current_color.red)
                 | (uint32_t(current_color.green) << 8)
                 | (uint32_t(current_color.blue)  << 16)
                 | (uint32_t(current_color.alpha) << 24);
    return s;
}

DeferredPainterState deferred_renderer::capture_painter_state() const
{
    DeferredPainterState state;
    state.coordinate_system = current_coordinate_system;
    state.draw_color = current_color;
    state.line_width = current_line_width;
    state.line_cap_style = current_line_cap;
    state.line_dash_style = current_line_dash;
    state.rotation_radians = rotation_angle;
    state.horiz_just = horiz_justification;
    state.vert_just = vert_justification;
    state.font = current_font;
    return state;
}

void deferred_renderer::apply_painter_state(const DeferredPainterState& state)
{
    current_coordinate_system = state.coordinate_system;
    rotation_angle = state.rotation_radians;
    horiz_justification = state.horiz_just;
    vert_justification = state.vert_just;
    current_font = state.font;
    m_painter->setFont(current_font);
    // Call base renderer methods explicitly to avoid recursive dispatch.
    irenderer::set_color(state.draw_color);
    irenderer::set_line_width(state.line_width);
    irenderer::set_line_cap(state.line_cap_style);
    irenderer::set_line_dash(state.line_dash_style);
}

// ---- batch insertion -----------------------------------------------------

void deferred_renderer::add_line(const LineStyleKey &s, QLineF line)
{
    uint64_t k = s.key() ^ (uint64_t(1) << 60);
    auto it = m_line_idx.find(k);
    if (it == m_line_idx.end()) {
        m_line_idx[k] = m_line_batches.size();
        m_line_batches.push_back({s, {}});
        it = m_line_idx.find(k);
    }
    m_line_batches[it->second].lines.push_back(line);
}

void deferred_renderer::add_fill_rect(const FillStyleKey &s, QRectF rect)
{
    uint64_t k = s.key() ^ (uint64_t(2) << 60);
    auto it = m_fill_rect_idx.find(k);
    if (it == m_fill_rect_idx.end()) {
        m_fill_rect_idx[k] = m_fill_rect_batches.size();
        m_fill_rect_batches.push_back({s, {}});
        it = m_fill_rect_idx.find(k);
    }
    m_fill_rect_batches[it->second].rects.push_back(rect);
}

void deferred_renderer::add_draw_rect(const LineStyleKey &s, QRectF rect)
{
    uint64_t k = s.key() ^ (uint64_t(3) << 60);
    auto it = m_draw_rect_idx.find(k);
    if (it == m_draw_rect_idx.end()) {
        m_draw_rect_idx[k] = m_draw_rect_batches.size();
        m_draw_rect_batches.push_back({s, {}});
        it = m_draw_rect_idx.find(k);
    }
    m_draw_rect_batches[it->second].rects.push_back(rect);
}

void deferred_renderer::add_fill_poly(const FillStyleKey &s, QPolygonF poly)
{
    uint64_t k = s.key() ^ (uint64_t(4) << 60);
    auto it = m_fill_poly_idx.find(k);
    if (it == m_fill_poly_idx.end()) {
        m_fill_poly_idx[k] = m_fill_poly_batches.size();
        m_fill_poly_batches.push_back({s, {}});
        it = m_fill_poly_idx.find(k);
    }
    m_fill_poly_batches[it->second].polys.push_back(std::move(poly));
}

// ---- coordinate helper ---------------------------------------------------

QRectF deferred_renderer::to_screen_rect(const point2d& start, const point2d& end)
{
    point2d rect_start = start;
    point2d rect_end = end;
    if (current_coordinate_system == WORLD) {
        rect_start = m_camera->world_to_screen(rect_start);
        rect_end   = m_camera->world_to_screen(rect_end);
    }
    double x = std::min(rect_start.x, rect_end.x);
    double y = std::min(rect_start.y, rect_end.y);
    double w = std::abs(rect_end.x - rect_start.x);
    double h = std::abs(rect_end.y - rect_start.y);
    return QRectF(x, y, w, h);
}

QRectF deferred_renderer::screen_viewport_rect() const
{
    rectangle viewport = irenderer::get_visible_screen();
    return QRectF(viewport.left(), viewport.bottom(), viewport.width(), viewport.height());
}

bool deferred_renderer::screen_rect_visible(const QRectF& rect, double padding) const
{
    QRectF padded = rect.normalized().adjusted(-padding, -padding, padding, padding);
    return padded.intersects(screen_viewport_rect());
}

bool deferred_renderer::screen_line_visible(const QLineF& line, double line_width) const
{
    const double half_width = std::max(1.0, line_width) * 0.5;
    const QRectF viewport = screen_viewport_rect().adjusted(-half_width, -half_width,
                                                            half_width, half_width);
    const QRectF bounds = QRectF(line.p1(), line.p2()).normalized().adjusted(-half_width,
                                                                              -half_width,
                                                                              half_width,
                                                                              half_width);
    if (!bounds.intersects(viewport))
        return false;

    if (viewport.contains(line.p1()) || viewport.contains(line.p2()))
        return true;

    const QLineF edges[] = {
        QLineF(viewport.topLeft(), viewport.topRight()),
        QLineF(viewport.topRight(), viewport.bottomRight()),
        QLineF(viewport.bottomRight(), viewport.bottomLeft()),
        QLineF(viewport.bottomLeft(), viewport.topLeft())
    };

    QPointF intersection;
    for (const QLineF& edge : edges) {
        if (line.intersects(edge, &intersection) == QLineF::BoundedIntersection)
            return true;
    }

    return false;
}

bool deferred_renderer::screen_arc_visible(const point2d& center,
                                           double radius_x,
                                           double radius_y) const
{
    return screen_rect_visible(QRectF(center.x - radius_x,
                                      center.y - radius_y,
                                      2.0 * radius_x,
                                      2.0 * radius_y));
}

bool deferred_renderer::screen_text_visible(const point2d& point,
                                            const std::string& text,
                                            double bound_x,
                                            double bound_y) const
{
    text_extents_t text_extents{0, 0, 0, 0, 0, 0};
    m_painter->text_extents(text.c_str(), &text_extents);

    const bool bounded_x = std::isfinite(bound_x) && bound_x < DBL_MAX;
    const bool bounded_y = std::isfinite(bound_y) && bound_y < DBL_MAX;
    if ((bounded_x && text_extents.width > bound_x)
        || (bounded_y && text_extents.height > bound_y)) {
        return false;
    }
    const double clip_width = bounded_x ? bound_x : text_extents.width;
    const double clip_height = bounded_y ? bound_y : text_extents.height;

    point2d center = point;
    if (horiz_justification == justification::left)
        center.x += clip_width / 2.0;
    else if (horiz_justification == justification::right)
        center.x -= clip_width / 2.0;
    if (vert_justification == justification::top)
        center.y -= clip_height / 2.0;
    else if (vert_justification == justification::bottom)
        center.y += clip_height / 2.0;

    return screen_rect_visible(QRectF(center.x - clip_width / 2.0,
                                      center.y - clip_height / 2.0,
                                      clip_width,
                                      clip_height));
}

bool deferred_renderer::screen_surface_visible(surface *p_surface,
                                               const point2d& point,
                                               double scale_factor) const
{
    if (p_surface == nullptr || p_surface->isNull())
        return false;

    const double s_width = double(p_surface->width()) * scale_factor;
    const double s_height = double(p_surface->height()) * scale_factor;

    point2d top_left = point;
    if (horiz_justification == justification::center)
        top_left.x -= s_width / 2.0;
    else if (horiz_justification == justification::right)
        top_left.x -= s_width;

    if (vert_justification == justification::center)
        top_left.y -= s_height / 2.0;
    else if (vert_justification == justification::bottom)
        top_left.y -= s_height;

    return screen_rect_visible(QRectF(top_left.x, top_left.y, s_width, s_height));
}

// ---- hot-path draw calls (batched) ---------------------------------------

void deferred_renderer::draw_line(const point2d& start, const point2d& end)
{
    if (rectangle_off_screen({start, end}))
        return;

    point2d draw_start = start;
    point2d draw_end = end;
    if (current_coordinate_system == WORLD) {
        rectangle clip = irenderer::get_visible_world();
        if (!clip_line_world(clip, draw_start, draw_end))
            return;
        draw_start = m_camera->world_to_screen(draw_start);
        draw_end   = m_camera->world_to_screen(draw_end);
    }

    add_line(current_line_style(), QLineF(draw_start.x, draw_start.y, draw_end.x, draw_end.y));
}

void deferred_renderer::fill_rectangle(const point2d& start, const point2d& end)
{
    if (rectangle_off_screen({start, end}))
        return;
    add_fill_rect(current_fill_style(), to_screen_rect(start, end));
}

void deferred_renderer::fill_rectangle(const point2d& start, double width, double height)
{
    fill_rectangle(start, {start.x + width, start.y + height});
}

void deferred_renderer::fill_rectangle(const rectangle& r)
{
    fill_rectangle({r.left(), r.bottom()}, {r.right(), r.top()});
}

void deferred_renderer::draw_rectangle(const point2d& start, const point2d& end)
{
    if (rectangle_off_screen({start, end}))
        return;
    add_draw_rect(current_line_style(), to_screen_rect(start, end));
}

void deferred_renderer::draw_rectangle(const point2d& start, double width, double height)
{
    draw_rectangle(start, {start.x + width, start.y + height});
}

void deferred_renderer::draw_rectangle(const rectangle& r)
{
    draw_rectangle({r.left(), r.bottom()}, {r.right(), r.top()});
}

// ---- draw calls (stored in the command queue) ----------------------------

void deferred_renderer::fill_triangle(const point2d& a, const point2d& b, const point2d& c)
{
    QPolygonF poly(3);
    if (current_coordinate_system == WORLD) {
        const point2d sa = m_camera->world_to_screen(a);
        const point2d sb = m_camera->world_to_screen(b);
        const point2d sc = m_camera->world_to_screen(c);
        poly[0] = QPointF(sa.x, sa.y);
        poly[1] = QPointF(sb.x, sb.y);
        poly[2] = QPointF(sc.x, sc.y);
    } else {
        poly[0] = QPointF(a.x, a.y);
        poly[1] = QPointF(b.x, b.y);
        poly[2] = QPointF(c.x, c.y);
    }
    add_fill_poly(current_fill_style(), std::move(poly));
}

void deferred_renderer::fill_poly(const std::vector<point2d>& points)
{
    assert(points.size() > 3 && "if points.size() == 3 use fill_triangle method instead, it's much faster");

    QPolygonF poly;
    poly.reserve(int(points.size()));
    if (current_coordinate_system == WORLD) {
        for (const auto& p : points) {
            const point2d sp = m_camera->world_to_screen(p);
            poly.append(QPointF(sp.x, sp.y));
        }
    } else {
        for (const auto& p : points) {
            poly.append(QPointF(p.x, p.y));
        }
    }
    add_fill_poly(current_fill_style(), std::move(poly));
}

void deferred_renderer::push_arc_command(const point2d& center, double radius_x,
                                         double radius_y, double start_angle,
                                         double extent_angle, bool fill)
{
    const std::uint32_t command_index = std::uint32_t(m_commands.size());
    m_commands.emplace_back(DeferredArcCommand{
        capture_painter_state(),
        center,
        radius_x,
        radius_y,
        start_angle,
        extent_angle,
        fill
    });
    if (current_coordinate_system == WORLD) {
        index_world_command(
            command_index,
            {{center.x - radius_x, center.y - radius_y},
             {center.x + radius_x, center.y + radius_y}});
    } else {
        m_unindexed_commands.push_back(command_index);
    }
}

void deferred_renderer::draw_elliptic_arc(const point2d& center, double radius_x,
                                          double radius_y, double start_angle,
                                          double extent_angle)
{
    push_arc_command(center, radius_x, radius_y, start_angle, extent_angle, false);
}

void deferred_renderer::draw_arc(const point2d& center, double radius,
                                 double start_angle, double extent_angle)
{
    push_arc_command(center, radius, radius, start_angle, extent_angle, false);
}

void deferred_renderer::fill_elliptic_arc(const point2d& center, double radius_x,
                                          double radius_y, double start_angle,
                                          double extent_angle)
{
    push_arc_command(center, radius_x, radius_y, start_angle, extent_angle, true);
}

void deferred_renderer::fill_arc(const point2d& center, double radius,
                                 double start_angle, double extent_angle)
{
    push_arc_command(center, radius, radius, start_angle, extent_angle, true);
}

void deferred_renderer::fill_arrow_pointer_triangle(const point2d& anchor_world,
                                                     const point2d& dir_world,
                                                     float          arrow_size_px)
{
    // Compute the three corner offsets in PIXEL space against the current
    // line direction; record the world anchor and the offsets. At replay
    // time the anchor is reprojected through the (possibly different)
    // current camera, but the pixel offsets stay constant — the triangle's
    // screen size never grows on zoom-in, even under the camera-only
    // redraw path.
    const double dlen = std::hypot(dir_world.x, dir_world.y);
    const point2d dir_unit = (dlen > 1e-9)
        ? point2d{dir_world.x / dlen, dir_world.y / dlen}
        : point2d{1.0, 0.0};
    const point2d perp_unit{-dir_unit.y, dir_unit.x};
    const float r = arrow_size_px * 0.5f;

    const point2d off_tip {  dir_unit.x * r,                    dir_unit.y * r };
    const point2d off_left{ -dir_unit.x * r + perp_unit.x * r, -dir_unit.y * r + perp_unit.y * r };
    const point2d off_right{-dir_unit.x * r - perp_unit.x * r, -dir_unit.y * r - perp_unit.y * r };

    const std::uint32_t command_index = std::uint32_t(m_commands.size());
    m_commands.emplace_back(DeferredArrowTriangleCommand{
        capture_painter_state(),
        anchor_world,
        off_tip,
        off_left,
        off_right
    });
    // Mark as unindexed: we don't have a tight world-bbox to feed the
    // spatial index (the on-screen extent depends on the current camera),
    // so always replay.
    m_unindexed_commands.push_back(command_index);
}

void deferred_renderer::draw_text(const point2d& point, std::string const& text)
{
    draw_text(point, text, DBL_MAX, DBL_MAX);
}

void deferred_renderer::draw_text(const point2d& point, std::string const& text,
                                  double bound_x, double bound_y)
{
    const std::uint32_t command_index = std::uint32_t(m_commands.size());
    const point2d recorded_world_scale = m_camera->get_world_scale_factor();
    m_commands.emplace_back(DeferredTextCommand{
        capture_painter_state(),
        point,
        text,
        bound_x,
        bound_y,
        current_coordinate_system == WORLD,
        std::max(recorded_world_scale.x, std::numeric_limits<double>::epsilon()),
        text_screen_offset_px
    });
    // Auto-reset so the offset only applies to this draw_text. Mirrors
    // what irenderer::paint_text does in the immediate path.
    text_screen_offset_px = {0.0, 0.0};
    if (current_coordinate_system == WORLD) {
        text_extents_t text_extents{0, 0, 0, 0, 0, 0};
        m_painter->text_extents(text.c_str(), &text_extents);

        const bool bounded_x = std::isfinite(bound_x) && bound_x < DBL_MAX;
        const bool bounded_y = std::isfinite(bound_y) && bound_y < DBL_MAX;
        const double clip_width = bounded_x ? bound_x : text_extents.width * recorded_world_scale.x;
        const double clip_height = bounded_y ? bound_y : text_extents.height * recorded_world_scale.y;

        point2d center = point;
        if (horiz_justification == justification::left)
            center.x += clip_width / 2.0;
        else if (horiz_justification == justification::right)
            center.x -= clip_width / 2.0;
        if (vert_justification == justification::top)
            center.y -= clip_height / 2.0;
        else if (vert_justification == justification::bottom)
            center.y += clip_height / 2.0;

        index_world_command(
            command_index,
            {{center.x - clip_width / 2.0, center.y - clip_height / 2.0},
             clip_width,
             clip_height});
    } else {
        m_unindexed_commands.push_back(command_index);
    }
}

void deferred_renderer::draw_surface(surface *p_surface, const point2d& point,
                                     double scale_factor)
{
    const std::uint32_t command_index = std::uint32_t(m_commands.size());
    m_commands.emplace_back(DeferredSurfaceCommand{
        capture_painter_state(),
        p_surface,
        point,
        scale_factor
    });
    m_unindexed_commands.push_back(command_index);
}

// ---- flush / replay / reset ----------------------------------------------

// ---- replay helpers: per-command visibility ------------------------------

bool deferred_renderer::resolve_text_replay_state(const DeferredTextCommand& cmd,
                                                  DeferredPainterState& state)
{
    state = cmd.state;
    if (!cmd.scale_font_with_camera)
        return true;

    const double current_scale =
        std::max(m_camera->get_world_scale_factor().x, std::numeric_limits<double>::epsilon());
    const double scale_ratio = std::min(1.0, cmd.recorded_world_scale / current_scale);
    if (state.font.pixelSize() > 0) {
        const double scaled_pixel_size = state.font.pixelSize() * scale_ratio;
        if (scaled_pixel_size < kMinReadableTextSize)
            return false;
        state.font.setPixelSize(std::max(1, int(std::lround(scaled_pixel_size))));
    } else if (state.font.pointSizeF() > 0.0) {
        const double scaled_point_size = state.font.pointSizeF() * scale_ratio;
        if (scaled_point_size < kMinReadableTextSize)
            return false;
        state.font.setPointSizeF(scaled_point_size);
    }
    return true;
}

bool deferred_renderer::world_arc_visible(const point2d& center, double radius_x, double radius_y)
{
    return !rectangle_off_screen(
        {{center.x - radius_x, center.y - radius_y},
         {center.x + radius_x, center.y + radius_y}});
}

bool deferred_renderer::world_text_visible(const point2d& point, const std::string& text,
                                           double bound_x, double bound_y)
{
    const point2d world_scale = m_camera->get_world_scale_factor();
    if (bound_y / world_scale.y < MINIMAL_VISIBLE_TEXT_BOUND_Y_IN_PX)
        return false;

    text_extents_t text_extents{0, 0, 0, 0, 0, 0};
    m_painter->text_extents(text.c_str(), &text_extents);

    const double scaled_width = text_extents.width * world_scale.x;
    const double scaled_height = text_extents.height * world_scale.y;
    const bool bounded_x = std::isfinite(bound_x) && bound_x < DBL_MAX;
    const bool bounded_y = std::isfinite(bound_y) && bound_y < DBL_MAX;
    if ((bounded_x && scaled_width > bound_x)
        || (bounded_y && scaled_height > bound_y))
        return false;
    const double clip_width = bounded_x ? bound_x : scaled_width;
    const double clip_height = bounded_y ? bound_y : scaled_height;

    point2d center = point;
    if (horiz_justification == justification::left)
        center.x += clip_width / 2.0;
    else if (horiz_justification == justification::right)
        center.x -= clip_width / 2.0;
    if (vert_justification == justification::top)
        center.y -= clip_height / 2.0;
    else if (vert_justification == justification::bottom)
        center.y += clip_height / 2.0;

    return !rectangle_off_screen(
        {{center.x - clip_width / 2.0, center.y - clip_height / 2.0},
         clip_width, clip_height});
}

bool deferred_renderer::world_surface_visible(surface* p_surface, const point2d& point,
                                              double scale_factor)
{
    if (p_surface == nullptr || p_surface->isNull())
        return false;

    double s_width = double(p_surface->width()) * scale_factor;
    double s_height = double(p_surface->height()) * scale_factor;
    s_width *= m_camera->get_world_scale_factor().x;
    s_height *= m_camera->get_world_scale_factor().y;

    point2d top_left = point;
    if (horiz_justification == justification::center)
        top_left.x -= s_width / 2.0;
    else if (horiz_justification == justification::right)
        top_left.x -= s_width;
    if (vert_justification == justification::center)
        top_left.y += s_height / 2.0;
    else if (vert_justification == justification::bottom)
        top_left.y += s_height;

    return !rectangle_off_screen({{top_left.x, top_left.y - s_height}, s_width, s_height});
}

// ---- replay --------------------------------------------------------------

void deferred_renderer::replay()
{
    // Start the frame: empty the reused scratch (keeps capacity); the stage
    // functions below only append to it.
    m_replay_cache.clear();

    // Stage 1 — cull everything to what is on screen for the current camera.
    // The stage functions fill persistent scratch members (reused across
    // frames) rather than returning fresh vectors.
    cull_visible_batches(m_replay_cache.visible_batches);
    gather_candidate_commands(m_replay_cache.candidate_commands);
    select_visible_commands(m_replay_cache.candidate_commands, m_replay_cache.visible_commands);
    const VisibleBatches& batches = m_replay_cache.visible_batches;
    const std::vector<const DeferredCommand*>& commands = m_replay_cache.visible_commands;

#ifdef EZGL_RENDERER_DEBUG
    DeferredVisibleStats stats;
    for (const auto& b : batches.lines)      stats.lines          += b.lines.size();
    for (const auto& b : batches.fill_rects) stats.filled_rects   += b.rects.size();
    for (const auto& b : batches.draw_rects) stats.outlined_rects += b.rects.size();
    for (const auto& b : batches.fill_polys) stats.filled_polys   += b.polys.size();
    for (const DeferredCommand* command : commands) {
        std::visit([&](const auto& cmd) {
            using T = std::decay_t<decltype(cmd)>;
            if constexpr (std::is_same_v<T, DeferredArcCommand>)
                cmd.fill ? ++stats.filled_arcs : ++stats.outlined_arcs;
            else if constexpr (std::is_same_v<T, DeferredTextCommand>)    ++stats.texts;
            else if constexpr (std::is_same_v<T, DeferredSurfaceCommand>) ++stats.surfaces;
        }, *command);
    }
    print_visible_stats(stats);
#endif // EZGL_RENDERER_DEBUG

    // Stage 2 — paint: bulk batches first, then commands on top.
    draw_visible_batches(batches);
    draw_visible_commands(commands);
}

void deferred_renderer::cull_visible_batches(VisibleBatches& out) const
{
    // out is expected empty (replay() clears the cache); these only append.
    for (const auto& batch : m_line_batches) {
        std::vector<QLineF> visible_lines;
        visible_lines.reserve(batch.lines.size());
        const double line_width = batch.style.line_width == 0 ? 1.0 : double(batch.style.line_width);
        for (const QLineF& line : batch.lines)
            if (screen_line_visible(line, line_width))
                visible_lines.push_back(line);
        if (!visible_lines.empty())
            out.lines.push_back({batch.style, std::move(visible_lines)});
    }

    for (const auto& batch : m_fill_rect_batches) {
        std::vector<QRectF> visible_rects;
        visible_rects.reserve(batch.rects.size());
        for (const QRectF& rect : batch.rects)
            if (screen_rect_visible(rect))
                visible_rects.push_back(rect);
        if (!visible_rects.empty())
            out.fill_rects.push_back({batch.style, {}, std::move(visible_rects)});
    }

    for (const auto& batch : m_draw_rect_batches) {
        std::vector<QRectF> visible_rects;
        visible_rects.reserve(batch.rects.size());
        const double padding = (batch.style.line_width == 0 ? 1.0 : double(batch.style.line_width)) * 0.5;
        for (const QRectF& rect : batch.rects)
            if (screen_rect_visible(rect, padding))
                visible_rects.push_back(rect);
        if (!visible_rects.empty())
            out.draw_rects.push_back({{}, batch.style, std::move(visible_rects)});
    }

    for (const auto& batch : m_fill_poly_batches) {
        std::vector<QPolygonF> visible_polys;
        visible_polys.reserve(batch.polys.size());
        for (const QPolygonF& poly : batch.polys)
            if (screen_rect_visible(poly.boundingRect()))
                visible_polys.push_back(poly);
        if (!visible_polys.empty())
            out.fill_polys.push_back({batch.style, std::move(visible_polys)});
    }
}

void deferred_renderer::gather_candidate_commands(std::vector<std::uint32_t>& candidates)
{
    // candidates is expected empty (replay() clears the cache).
    // Unindexed commands (e.g. arrows, SCREEN-coordinate ones) have no world
    // bbox, so they are always candidates.
    candidates.insert(candidates.end(),
                      m_unindexed_commands.begin(),
                      m_unindexed_commands.end());

    const rectangle visible_world = irenderer::get_visible_world();

    if (!m_indexed_world_command_buckets.empty()
        && !m_commands.empty()
        && m_command_index_scene_bounds.right() >= visible_world.left()
        && m_command_index_scene_bounds.left() <= visible_world.right()
        && m_command_index_scene_bounds.top() >= visible_world.bottom()
        && m_command_index_scene_bounds.bottom() <= visible_world.top()) {
        // A per-query generation stamp dedups commands that span several tiles
        // without clearing the whole marks vector each frame.
        if (m_command_query_marks.size() < m_commands.size())
            m_command_query_marks.resize(m_commands.size(), 0);
        ++m_command_query_generation;
        if (m_command_query_generation == 0) {
            std::fill(m_command_query_marks.begin(), m_command_query_marks.end(), 0);
            m_command_query_generation = 1;
        }

        const int min_tx = clamp_command_tile_x(visible_world.left());
        const int max_tx = clamp_command_tile_x(visible_world.right());
        const int min_ty = clamp_command_tile_y(visible_world.bottom());
        const int max_ty = clamp_command_tile_y(visible_world.top());
        for (int ty = min_ty; ty <= max_ty; ++ty) {
            for (int tx = min_tx; tx <= max_tx; ++tx) {
                const std::size_t bucket_index =
                    std::size_t(ty) * std::size_t(kCommandSpatialGridDimension) + std::size_t(tx);
                for (const std::uint32_t command_index : m_indexed_world_command_buckets[bucket_index]) {
                    if (m_command_query_marks[command_index] == m_command_query_generation)
                        continue;
                    m_command_query_marks[command_index] = m_command_query_generation;
                    candidates.push_back(command_index);
                }
            }
        }
    }

    // Restore record order so replay matches submission order.
    std::sort(candidates.begin(), candidates.end());
}

void deferred_renderer::select_visible_commands(
    const std::vector<std::uint32_t>& candidates,
    std::vector<const DeferredCommand*>& visible)
{
    // visible is expected empty (replay() clears the cache).
    for (const std::uint32_t command_index : candidates) {
        const DeferredCommand& command =
            m_commands[std::size_t(command_index)];
        const bool is_visible = std::visit([&](const auto& cmd) -> bool {
            using T = std::decay_t<decltype(cmd)>;
            if constexpr (std::is_same_v<T, DeferredArcCommand>) {
                return cmd.state.coordinate_system == SCREEN
                    ? screen_arc_visible(cmd.center, cmd.radius_x, cmd.radius_y)
                    : world_arc_visible(cmd.center, cmd.radius_x, cmd.radius_y);
            } else if constexpr (std::is_same_v<T, DeferredTextCommand>) {
                DeferredPainterState state;
                if (!resolve_text_replay_state(cmd, state))
                    return false;
                apply_painter_state(state);   // text_extents needs the font set
                return state.coordinate_system == SCREEN
                    ? screen_text_visible(cmd.point, cmd.text, cmd.bound_x, cmd.bound_y)
                    : world_text_visible(cmd.point, cmd.text, cmd.bound_x, cmd.bound_y);
            } else if constexpr (std::is_same_v<T, DeferredSurfaceCommand>) {
                apply_painter_state(cmd.state);
                return cmd.state.coordinate_system == SCREEN
                    ? screen_surface_visible(cmd.p_surface, cmd.anchor_point, cmd.scale_factor)
                    : world_surface_visible(cmd.p_surface, cmd.anchor_point, cmd.scale_factor);
            } else if constexpr (std::is_same_v<T, DeferredArrowTriangleCommand>) {
                apply_painter_state(cmd.state);
                // Always visible — the on-screen extent is a few pixels and a
                // tight world-bbox would need the current camera scale.
                return true;
            }
            return false;
        }, command);

        if (is_visible)
            visible.push_back(&command);
    }
}

void deferred_renderer::draw_visible_batches(const VisibleBatches& batches)
{
    for (const auto& batch : batches.fill_rects) {
        QColor c = unpack_color(batch.fill_style.color_rgba);
        m_painter->setPen(Qt::NoPen);
        m_painter->setBrush(QBrush(c));
        m_painter->drawRects(batch.rects.data(), int(batch.rects.size()));
    }

    for (const auto& batch : batches.fill_polys) {
        QColor c = unpack_color(batch.style.color_rgba);
        m_painter->setPen(Qt::NoPen);
        m_painter->setBrush(QBrush(c));
        for (const QPolygonF& poly : batch.polys)
            m_painter->drawPolygon(poly);
    }

    for (const auto& batch : batches.draw_rects) {
        QPen pen = make_pen(batch.line_style);
        m_painter->setPen(pen);
        m_painter->setBrush(Qt::NoBrush);
        m_painter->drawRects(batch.rects.data(), int(batch.rects.size()));
    }

    for (const auto& batch : batches.lines) {
        QPen pen = make_pen(batch.style);
        m_painter->setPen(pen);
        m_painter->setBrush(Qt::NoBrush);
        m_painter->drawLines(batch.lines.data(), int(batch.lines.size()));
    }
}

void deferred_renderer::draw_visible_commands(
    const std::vector<const DeferredCommand*>& commands)
{
    for (const DeferredCommand* command : commands) {
        std::visit([this](const auto& cmd) {
            apply_painter_state(cmd.state);
            using T = std::decay_t<decltype(cmd)>;
            if constexpr (std::is_same_v<T, DeferredArcCommand>) {
                const double stretch = cmd.radius_x > 0.0
                    ? cmd.radius_y / cmd.radius_x : 1.0;
                paint_arc_path(cmd.center, cmd.radius_x, cmd.start_angle,
                               cmd.extent_angle, stretch, cmd.fill);
            } else if constexpr (std::is_same_v<T, DeferredTextCommand>) {
                DeferredPainterState state = cmd.state;
                if (!resolve_text_replay_state(cmd, state))
                    return;
                apply_painter_state(state);
                // Restore the per-command screen offset; paint_text consumes
                // it (and resets to 0) so it doesn't leak into the next cmd.
                text_screen_offset_px = cmd.screen_offset_px;
                paint_text(cmd.point, cmd.text, cmd.bound_x, cmd.bound_y);
            } else if constexpr (std::is_same_v<T, DeferredSurfaceCommand>) {
                paint_surface(cmd.p_surface, cmd.anchor_point, cmd.scale_factor);
            } else if constexpr (std::is_same_v<T, DeferredArrowTriangleCommand>) {
                // Project anchor with the CURRENT camera, then add the
                // recorded pixel offsets. Net effect: the triangle's
                // anchor moves with the world, but its size on screen is
                // exactly the same pixel extent at every zoom level.
                const point2d anchor_screen = m_camera->world_to_screen(cmd.anchor_world);
                const QPointF pa(anchor_screen.x + cmd.offset_a_px.x,
                                 anchor_screen.y + cmd.offset_a_px.y);
                const QPointF pb(anchor_screen.x + cmd.offset_b_px.x,
                                 anchor_screen.y + cmd.offset_b_px.y);
                const QPointF pc(anchor_screen.x + cmd.offset_c_px.x,
                                 anchor_screen.y + cmd.offset_c_px.y);
                QPolygonF poly;
                poly << pa << pb << pc;
                const color& dc = cmd.state.draw_color;
                m_painter->setPen(Qt::NoPen);
                m_painter->setBrush(QBrush(QColor(int(dc.red), int(dc.green),
                                                  int(dc.blue), int(dc.alpha))));
                m_painter->drawPolygon(poly);
            }
        }, *command);
    }
}

void deferred_renderer::flush()
{
    replay();
    reset();
}

void deferred_renderer::reset()
{
    m_line_batches.clear();
    m_fill_rect_batches.clear();
    m_draw_rect_batches.clear();
    m_fill_poly_batches.clear();
    m_line_idx.clear();
    m_fill_rect_idx.clear();
    m_draw_rect_idx.clear();
    m_fill_poly_idx.clear();
    m_commands.clear();
    for (auto& bucket : m_indexed_world_command_buckets)
        bucket.clear();
    m_unindexed_commands.clear();
    m_command_query_marks.clear();
    m_command_query_generation = 1;
    // Drop the replay scratch (esp. the command pointers, which would dangle
    // once m_commands is cleared); clear() keeps the heap capacity.
    m_replay_cache.clear();
}

} // namespace ezgl
