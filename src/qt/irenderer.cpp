#include "ezgl/irenderer.hpp"
#include "ezgl/camera.hpp"
#include "ezgl/logutils.hpp"
#include "ezgl/qt/painter.hpp"
#include "ezgl/qt/qtutils.hpp"

#include <algorithm>
#include <cassert>
#include <cfloat>
#include <cmath>
#include <numbers>
#include <utility>
#include <QFile>

namespace ezgl {

// ---- Liang-Barsky line clipping (file-scope helpers) -------------------------

static bool test_clipping_edge(double dir, double dist, double& t_enter, double& t_exit)
{
    if (dir < 0.0) {
        double t = dist / dir;
        if (t > t_exit) return false;
        if (t > t_enter) t_enter = t;
    } else if (dir > 0.0) {
        double t = dist / dir;
        if (t < t_enter) return false;
        if (t < t_exit) t_exit = t;
    } else {
        if (dist < 0.0) return false;
    }
    return true;
}

static bool clip_line(const rectangle& w, point2d& p1, point2d& p2)
{
    double dx = p2.x - p1.x;
    double dy = p2.y - p1.y;
    double t_enter = 0.0, t_exit = 1.0;

    if (!test_clipping_edge(-dx, p1.x - w.left(),   t_enter, t_exit)) return false;
    if (!test_clipping_edge( dx, w.right() - p1.x,  t_enter, t_exit)) return false;
    if (!test_clipping_edge(-dy, p1.y - w.bottom(),  t_enter, t_exit)) return false;
    if (!test_clipping_edge( dy, w.top() - p1.y,     t_enter, t_exit)) return false;

    if (t_exit < 1.0) { p2.x = p1.x + t_exit * dx; p2.y = p1.y + t_exit * dy; }
    if (t_enter > 0.0) { p1.x += t_enter * dx; p1.y += t_enter * dy; }
    return true;
}

// ---- Construction ------------------------------------------------------------

irenderer::irenderer(Painter* painter, transform_fn transform, camera* cam, QImage*)
    : m_painter(painter)
    , m_transform(std::move(transform))
    , m_camera(cam)
{
    if (m_painter != nullptr)
        current_font = m_painter->font();
}

// ---- Painter / camera utilities ----------------------------------------------

void irenderer::update_painter(Painter* painter, QImage*)
{
    m_painter = painter;
    if (m_painter != nullptr) {
        m_painter->setFont(current_font);
    }
    irenderer::set_color(current_color);
    irenderer::set_line_width(current_line_width);
    irenderer::set_line_cap(current_line_cap);
    irenderer::set_line_dash(current_line_dash);
}

bool irenderer::rectangle_off_screen(rectangle rect)
{
    if (current_coordinate_system == SCREEN)
        return false;

    rectangle visible = irenderer::get_visible_world();
    return rect.right()  < visible.left()
        || rect.left()   > visible.right()
        || rect.top()    < visible.bottom()
        || rect.bottom() > visible.top();
}

bool irenderer::clip_line_world(const rectangle& clip_window, point2d& start, point2d& end)
{
    return clip_line(clip_window, start, end);
}

rectangle irenderer::get_visible_world()
{
    rectangle world  = m_camera->get_world();
    rectangle screen = m_camera->get_screen();
    point2d   margin = screen.bottom_left() * m_camera->get_world_scale_factor();
    return {(world.bottom_left() - margin), (world.top_right() + margin)};
}

rectangle irenderer::get_visible_screen() const
{
    return m_camera->get_widget();
}

rectangle irenderer::world_to_screen(const rectangle& box)
{
    return rectangle(m_transform(box.bottom_left()), m_transform(box.top_right()));
}

// ---- State-setter implementations -------------------------------------------

void irenderer::set_color(color c)
{
    irenderer::set_color(c.red, c.green, c.blue, c.alpha);
}

void irenderer::set_color(color c, uint_fast8_t alpha)
{
    irenderer::set_color(c.red, c.green, c.blue, alpha);
}

void irenderer::set_color(uint_fast8_t r, uint_fast8_t g, uint_fast8_t b, uint_fast8_t a)
{
    m_painter->set_source_rgba(r / 255.0, g / 255.0, b / 255.0, a / 255.0);
    current_color = {r, g, b, a};
}

void irenderer::set_line_cap(line_cap cap)
{
    m_painter->set_line_cap(static_cast<Qt::PenCapStyle>(cap));
    current_line_cap = cap;
}

void irenderer::set_line_dash(line_dash dash)
{
    if (dash == line_dash::none) {
        m_painter->set_dash(nullptr, 0, 0);
    } else if (dash == line_dash::asymmetric_5_3) {
        static double dashes[] = {5.0, 3.0};
        m_painter->set_dash(dashes, 2, 0);
    }
    current_line_dash = dash;
}

void irenderer::set_line_width(int width)
{
    m_painter->set_line_width(width == 0 ? 1 : width);
    current_line_width = width;
}

void irenderer::set_font_size(double size)
{
    current_font.setPixelSize(std::max(1, int(std::lround(size))));
    m_painter->set_font_size(size);
}

void irenderer::format_font(const std::string& family, font_slant slant, font_weight weight)
{
    current_font.setFamily(QString::fromStdString(family));
    current_font.setStyle(static_cast<QFont::Style>(slant));
    current_font.setWeight(static_cast<QFont::Weight>(weight));
    m_painter->select_font_face(family.c_str(),
                                static_cast<QFont::Style>(slant),
                                static_cast<QFont::Weight>(weight));
}

void irenderer::format_font(const std::string& family, font_slant slant,
                           font_weight weight, double new_size)
{
    irenderer::set_font_size(new_size);
    irenderer::format_font(family, slant, weight);
}

void irenderer::set_text_rotation(double degrees)
{
    if (degrees >= -360.0 && degrees <= 360.0)
        rotation_angle = -degrees * std::numbers::pi / 180.0;
    else
        q_warning("set_text_rotation: bad angle %f — ignored", degrees);
}

void irenderer::set_horiz_justification(justification j)
{
    if (j != justification::top && j != justification::bottom)
        horiz_justification = j;
}

void irenderer::set_vert_justification(justification j)
{
    if (j != justification::right && j != justification::left)
        vert_justification = j;
}

void irenderer::set_coordinate_system(t_coordinate_system cs)
{
    current_coordinate_system = cs;
}

void irenderer::set_visible_world(rectangle new_world)
{
    point2d n_center = new_world.center();
    double  n_width  = new_world.width();
    double  n_height = new_world.height();

    double i_width  = m_camera->get_initial_world().width();
    double i_height = m_camera->get_initial_world().height();
    double i_aspect = i_width / i_height;

    if (n_width / i_aspect >= n_height) {
        double new_h = n_width / i_aspect;
        new_world = {{n_center.x - n_width / 2, n_center.y - new_h / 2}, n_width, new_h};
    } else {
        double new_w = n_height * i_aspect;
        new_world = {{n_center.x - new_w / 2, n_center.y - n_height / 2}, new_w, n_height};
    }
    m_camera->set_world(new_world);
}

// ---- Immediate-mode paint helpers -------------------------------------------

void irenderer::paint_line(const point2d& start, const point2d& end)
{
    if (rectangle_off_screen({start, end}))
        return;

    point2d draw_start = start;
    point2d draw_end = end;
    if (current_coordinate_system == WORLD) {
        rectangle clip = irenderer::get_visible_world();
        if (!clip_line(clip, draw_start, draw_end))
            return;
        draw_start = m_transform(draw_start);
        draw_end   = m_transform(draw_end);
    }

    m_painter->move_to(draw_start.x, draw_start.y);
    m_painter->line_to(draw_end.x, draw_end.y);
    m_painter->stroke();
}

void irenderer::paint_rectangle_path(const point2d& start, const point2d& end, bool fill)
{
    point2d draw_start = start;
    point2d draw_end = end;
    if (current_coordinate_system == WORLD) {
        draw_start = m_transform(draw_start);
        draw_end   = m_transform(draw_end);
    }
    m_painter->move_to(draw_start.x, draw_start.y);
    m_painter->line_to(draw_start.x, draw_end.y);
    m_painter->line_to(draw_end.x,   draw_end.y);
    m_painter->line_to(draw_end.x,   draw_start.y);
    m_painter->close_path();
    if (fill) m_painter->fill();
    else      m_painter->stroke();
}

void irenderer::paint_poly(const std::vector<point2d>& points)
{
    assert(points.size() > 1);

    double x_min = points[0].x, x_max = points[0].x;
    double y_min = points[0].y, y_max = points[0].y;
    for (std::size_t i = 1; i < points.size(); ++i) {
        x_min = std::min(x_min, points[i].x);
        x_max = std::max(x_max, points[i].x);
        y_min = std::min(y_min, points[i].y);
        y_max = std::max(y_max, points[i].y);
    }
    if (rectangle_off_screen({{x_min, y_min}, {x_max, y_max}}))
        return;

    point2d first = (current_coordinate_system == WORLD) ? m_transform(points[0]) : points[0];
    m_painter->move_to(first.x, first.y);
    for (std::size_t i = 1; i < points.size(); ++i) {
        point2d p = (current_coordinate_system == WORLD) ? m_transform(points[i]) : points[i];
        m_painter->line_to(p.x, p.y);
    }
    m_painter->close_path();
    m_painter->fill();
}

void irenderer::paint_arc_path(const point2d& center, double radius, double start_angle,
                               double extent_angle, double stretch_factor, bool fill)
{
    point2d draw_center = center;
    point2d point_x = {draw_center.x + radius, draw_center.y};
    if (current_coordinate_system == WORLD) {
        draw_center  = m_transform(draw_center);
        point_x = m_transform(point_x);
    }
    radius = point_x.x - draw_center.x;

    m_painter->save();
    m_painter->scale(1.0 / stretch_factor, 1.0);
    draw_center.x = draw_center.x * stretch_factor;
    radius   = radius   * stretch_factor;
    m_painter->new_path();

    if (fill) {
        m_painter->move_to(draw_center.x, draw_center.y);
    } else {
        double a0_rad = -start_angle * std::numbers::pi / 180.0;
        m_painter->move_to(draw_center.x + radius * std::cos(a0_rad),
                           draw_center.y + radius * std::sin(a0_rad));
    }

    double end_angle = start_angle + extent_angle;
    if (extent_angle >= 0) {
        m_painter->arc_negative(draw_center.x, draw_center.y, radius,
                                -start_angle * std::numbers::pi / 180.0,
                                -end_angle   * std::numbers::pi / 180.0);
    } else {
        m_painter->arc(draw_center.x, draw_center.y, radius,
                       -start_angle * std::numbers::pi / 180.0,
                       -end_angle   * std::numbers::pi / 180.0);
    }

    if (fill) m_painter->close_path();
    if (fill) m_painter->fill();
    else      m_painter->stroke();
    m_painter->restore();
}

void irenderer::paint_text(const point2d& point, const std::string& text,
                           double bound_x, double bound_y)
{
    text_extents_t text_extents{0, 0, 0, 0, 0, 0};
    m_painter->text_extents(text.c_str(), &text_extents);

    point2d world_scale = m_camera->get_world_scale_factor();
    double  scaled_width, scaled_height;
    if (current_coordinate_system == WORLD) {
        scaled_width  = text_extents.width  * world_scale.x;
        scaled_height = text_extents.height * world_scale.y;
    } else {
        scaled_width  = text_extents.width;
        scaled_height = text_extents.height;
    }

    const bool bounded_x = std::isfinite(bound_x) && bound_x < DBL_MAX;
    const bool bounded_y = std::isfinite(bound_y) && bound_y < DBL_MAX;
    const double clip_w  = bounded_x ? bound_x : scaled_width;
    const double clip_h  = bounded_y ? bound_y : scaled_height;

    if ((bounded_x && scaled_width  > bound_x) ||
        (bounded_y && scaled_height > bound_y))
        return;

    point2d center = point;
    if (horiz_justification == justification::left)  center.x += clip_w / 2.0;
    else if (horiz_justification == justification::right) center.x -= clip_w / 2.0;
    if (vert_justification == justification::top)    center.y -= clip_h / 2.0;
    else if (vert_justification == justification::bottom) center.y += clip_h / 2.0;

    if (rectangle_off_screen({{center.x - clip_w / 2.0, center.y - clip_h / 2.0}, clip_w, clip_h}))
        return;

    auto local_clip_x = [](justification just, double extent) {
        if (just == justification::left)  return 0.0;
        if (just == justification::right) return -extent;
        return -extent / 2.0;
    };
    auto local_clip_y = [](justification just, double extent) {
        if (just == justification::bottom) return 0.0;
        if (just == justification::top)    return -extent;
        return -extent / 2.0;
    };

    point2d draw_center = (current_coordinate_system == WORLD) ? m_transform(point) : point;

    QString qtext = QString::fromStdString(text);
    QFontMetricsF fm(m_painter->font());
    QRectF br = fm.boundingRect(qtext);

    m_painter->save();
    m_painter->translate(draw_center.x, draw_center.y);
    m_painter->rotate(rotation_angle * 180.0 / std::numbers::pi);

    QPointF offset(-(br.x() + br.width() / 2.0), -(br.y() + br.height() / 2.0));
    if (horiz_justification == justification::left)  offset.rx() += br.width()  / 2.0;
    else if (horiz_justification == justification::right) offset.rx() -= br.width()  / 2.0;
    if (vert_justification == justification::top)    offset.ry() += br.height() / 2.0;
    else if (vert_justification == justification::bottom) offset.ry() -= br.height() / 2.0;

    if (bounded_x || bounded_y) {
        const QRectF text_rect = br.translated(offset);
        const double cw_screen = bounded_x
            ? (current_coordinate_system == WORLD ? bound_x / std::max(world_scale.x, DBL_EPSILON) : bound_x)
            : text_rect.width() + 2.0;
        const double ch_screen = bounded_y
            ? (current_coordinate_system == WORLD ? bound_y / std::max(world_scale.y, DBL_EPSILON) : bound_y)
            : text_rect.height() + 2.0;
        const QRectF clip_rect(bounded_x ? local_clip_x(horiz_justification, cw_screen) : text_rect.left() - 1.0,
                               bounded_y ? local_clip_y(vert_justification,  ch_screen) : text_rect.top()  - 1.0,
                               cw_screen, ch_screen);
        m_painter->setClipRect(clip_rect, Qt::IntersectClip);
    }

    m_painter->setPen(QColor(current_color.red, current_color.green,
                             current_color.blue, current_color.alpha));
    m_painter->drawText(offset, qtext);
    m_painter->restore();
}

void irenderer::paint_surface(surface* p_surface, const point2d& anchor, double scale_factor)
{
    if (p_surface == nullptr || p_surface->isNull()) {
        q_warning("draw_surface: null/invalid surface at %p", (void*)p_surface);
        return;
    }

    double s_width  = double(p_surface->width())  * scale_factor;
    double s_height = double(p_surface->height()) * scale_factor;
    if (current_coordinate_system == WORLD) {
        s_width  *= m_camera->get_world_scale_factor().x;
        s_height *= m_camera->get_world_scale_factor().y;
    }

    point2d top_left = anchor;
    if (horiz_justification == justification::center) top_left.x -= s_width / 2.0;
    else if (horiz_justification == justification::right) top_left.x -= s_width;
    if (vert_justification == justification::center)
        top_left.y += (current_coordinate_system == WORLD) ?  s_height / 2.0 : -s_height / 2.0;
    else if (vert_justification == justification::bottom)
        top_left.y += (current_coordinate_system == WORLD) ?  s_height : -s_height;

    if (rectangle_off_screen({{top_left.x, top_left.y - s_height}, s_width, s_height}))
        return;

    if (current_coordinate_system == WORLD)
        top_left = m_transform(top_left);

    if (scale_factor != 1.0) {
        m_painter->save();
        m_painter->scale(scale_factor, scale_factor);
        top_left.x /= scale_factor;
        top_left.y /= scale_factor;
    }
    m_painter->set_source_surface(p_surface, top_left.x, top_left.y);
    m_painter->paint();
    if (scale_factor != 1.0)
        m_painter->restore();
}

// ---- Static utility methods (irenderer) -------------------------------------

surface* irenderer::load_png(const char* file_path)
{
    QImage* image = new QImage;
    if (!QFile::exists(QString::fromLatin1(file_path)))
        q_warning("load_png: file %s not found", file_path);
    if (!image->load(QString::fromLatin1(file_path)))
        q_warning("load_png: error loading %s", file_path);
    return image;
}

void irenderer::free_surface(surface* p_surface)
{
    delete p_surface;
}

} // namespace ezgl
