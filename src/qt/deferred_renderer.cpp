#ifdef EZGL_QT

#include "ezgl/qt/deferred_renderer.hpp"
#include "ezgl/camera.hpp"

#include <QPen>
#include <QBrush>
#include <QColor>
#include <algorithm>
#include <type_traits>
#include <variant>

namespace ezgl {

// ---- helpers -------------------------------------------------------------

static constexpr double kMinReadableTextSize = 8.0;

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
        // asymmetric_5_3 pattern (same as Painter::set_dash)
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
                                     transform_fn transform,
                                     camera *cam,
                                     QImage *surface)
    : renderer(painter, std::move(transform), cam, surface)
{}

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
    set_color(state.draw_color);
    set_line_width(state.line_width);
    set_line_cap(state.line_cap_style);
    set_line_dash(state.line_dash_style);
}

// ---- batch insertion -----------------------------------------------------

void deferred_renderer::add_line(const LineStyleKey &s, QLineF line)
{
    // type tag in top 4 bits to distinguish from draw_rect with same style
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

// ---- coordinate helper ---------------------------------------------------

QRectF deferred_renderer::to_screen_rect(point2d start, point2d end)
{
    if (current_coordinate_system == WORLD) {
        start = m_transform(start);
        end   = m_transform(end);
    }
    double x = std::min(start.x, end.x);
    double y = std::min(start.y, end.y);
    double w = std::abs(end.x - start.x);
    double h = std::abs(end.y - start.y);
    return QRectF(x, y, w, h);
}

// ---- overridden draw calls -----------------------------------------------

void deferred_renderer::draw_line(point2d start, point2d end)
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

    add_line(current_line_style(), QLineF(start.x, start.y, end.x, end.y));
}

void deferred_renderer::fill_rectangle(point2d start, point2d end)
{
    if (rectangle_off_screen({start, end}))
        return;
    add_fill_rect(current_fill_style(), to_screen_rect(start, end));
}

void deferred_renderer::fill_rectangle(point2d start, double width, double height)
{
    fill_rectangle(start, {start.x + width, start.y + height});
}

void deferred_renderer::fill_rectangle(rectangle r)
{
    fill_rectangle({r.left(), r.bottom()}, {r.right(), r.top()});
}

void deferred_renderer::draw_rectangle(point2d start, point2d end)
{
    if (rectangle_off_screen({start, end}))
        return;
    add_draw_rect(current_line_style(), to_screen_rect(start, end));
}

void deferred_renderer::draw_rectangle(point2d start, double width, double height)
{
    draw_rectangle(start, {start.x + width, start.y + height});
}

void deferred_renderer::draw_rectangle(rectangle r)
{
    draw_rectangle({r.left(), r.bottom()}, {r.right(), r.top()});
}

// ---- flush ---------------------------------------------------------------

bool deferred_renderer::defer_fill_poly(const std::vector<point2d>& points)
{
    if (m_replaying_commands)
        return false;

    m_overlay_commands.emplace_back(DeferredPolyCommand{capture_painter_state(), points});
    return true;
}

bool deferred_renderer::defer_arc(point2d center,
                                  double radius_x,
                                  double radius_y,
                                  double start_angle,
                                  double extent_angle,
                                  bool fill)
{
    if (m_replaying_commands)
        return false;

    m_overlay_commands.emplace_back(DeferredArcCommand{
        capture_painter_state(),
        center,
        radius_x,
        radius_y,
        start_angle,
        extent_angle,
        fill
    });
    return true;
}

bool deferred_renderer::defer_text(point2d point,
                                   const std::string& text,
                                   double bound_x,
                                   double bound_y)
{
    if (m_replaying_commands)
        return false;

    m_overlay_commands.emplace_back(DeferredTextCommand{
        capture_painter_state(),
        point,
        text,
        bound_x,
        bound_y,
        current_coordinate_system == WORLD,
        std::max(m_camera->get_world_scale_factor().x, std::numeric_limits<double>::epsilon())
    });
    return true;
}

bool deferred_renderer::defer_surface(surface *p_surface,
                                      point2d point,
                                      double scale_factor)
{
    if (m_replaying_commands)
        return false;

    m_overlay_commands.emplace_back(DeferredSurfaceCommand{
        capture_painter_state(),
        p_surface,
        point,
        scale_factor
    });
    return true;
}

void deferred_renderer::replay()
{
    // lines
    for (const auto &batch : m_line_batches) {
        QPen pen = make_pen(batch.style);
        m_painter->setPen(pen);
        m_painter->setBrush(Qt::NoBrush);
        m_painter->drawLines(batch.lines.data(), int(batch.lines.size()));
    }

    // filled rects
    for (const auto &batch : m_fill_rect_batches) {
        QColor c = unpack_color(batch.style.color_rgba);
        m_painter->setPen(Qt::NoPen);
        m_painter->setBrush(QBrush(c));
        m_painter->drawRects(batch.rects.data(), int(batch.rects.size()));
    }

    // outline rects
    for (const auto &batch : m_draw_rect_batches) {
        QPen pen = make_pen(batch.style);
        m_painter->setPen(pen);
        m_painter->setBrush(Qt::NoBrush);
        m_painter->drawRects(batch.rects.data(), int(batch.rects.size()));
    }

    m_replaying_commands = true;
    for (const DeferredOverlayCommand& command : m_overlay_commands) {
        std::visit([this](const auto& cmd) {
            apply_painter_state(cmd.state);
            using T = std::decay_t<decltype(cmd)>;
            if constexpr (std::is_same_v<T, DeferredPolyCommand>) {
                renderer::fill_poly(cmd.points);
            } else if constexpr (std::is_same_v<T, DeferredArcCommand>) {
                if (cmd.fill) {
                    renderer::fill_elliptic_arc(cmd.center,
                                                cmd.radius_x,
                                                cmd.radius_y,
                                                cmd.start_angle,
                                                cmd.extent_angle);
                } else {
                    renderer::draw_elliptic_arc(cmd.center,
                                                cmd.radius_x,
                                                cmd.radius_y,
                                                cmd.start_angle,
                                                cmd.extent_angle);
                }
            } else if constexpr (std::is_same_v<T, DeferredTextCommand>) {
                DeferredPainterState state = cmd.state;
                if (cmd.scale_font_with_camera) {
                    const double current_scale =
                        std::max(m_camera->get_world_scale_factor().x, std::numeric_limits<double>::epsilon());
                    const double scale_ratio =
                        std::min(1.0, cmd.recorded_world_scale / current_scale);
                    if (state.font.pixelSize() > 0) {
                        const double scaled_pixel_size = state.font.pixelSize() * scale_ratio;
                        if (scaled_pixel_size < kMinReadableTextSize)
                            return;
                        state.font.setPixelSize(std::max(1, int(std::lround(scaled_pixel_size))));
                    } else if (state.font.pointSizeF() > 0.0) {
                        const double scaled_point_size = state.font.pointSizeF() * scale_ratio;
                        if (scaled_point_size < kMinReadableTextSize)
                            return;
                        state.font.setPointSizeF(scaled_point_size);
                    }
                }
                apply_painter_state(state);
                renderer::draw_text(cmd.point, cmd.text, cmd.bound_x, cmd.bound_y);
            } else if constexpr (std::is_same_v<T, DeferredSurfaceCommand>) {
                renderer::draw_surface(cmd.p_surface, cmd.anchor_point, cmd.scale_factor);
            }
        }, command);
    }
    m_replaying_commands = false;
}

void deferred_renderer::flush()
{
    replay();
    reset();
}

void deferred_renderer::clear_deferred_primitives()
{
    reset();
}

void deferred_renderer::reset()
{
    m_line_batches.clear();
    m_fill_rect_batches.clear();
    m_draw_rect_batches.clear();
    m_line_idx.clear();
    m_fill_rect_idx.clear();
    m_draw_rect_idx.clear();
    m_overlay_commands.clear();
}

} // namespace ezgl

#endif // EZGL_QT
