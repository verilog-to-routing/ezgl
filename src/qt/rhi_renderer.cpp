#if defined(EZGL_QT) && defined(EZGL_RHI)

#include "ezgl/qt/rhi_renderer.hpp"
#include "ezgl/camera.hpp"

#include <QtGlobal>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace {

std::uint32_t pack_color_rgba(const ezgl::color& c)
{
    return std::uint32_t(c.red)
         | (std::uint32_t(c.green) << 8)
         | (std::uint32_t(c.blue)  << 16)
         | (std::uint32_t(c.alpha) << 24);
}

} // namespace

namespace ezgl {

// ---- construction ----------------------------------------------------------

rhi_renderer::rhi_renderer(RhiCanvasWidget* widget,
                             transform_fn     transform,
                             camera*          cam,
                             draw_callback_fn draw_callback,
                             QColor           bg_color)
    : renderer(nullptr,           // painter is wired up below
               std::move(transform),
               cam,
               nullptr)           // surface not used in Qt path
    , m_rhi_widget(widget)
    , m_draw_callback(draw_callback)
    , m_bg_color(bg_color)
    , m_overlay(std::max(1, widget->width()),
                std::max(1, widget->height()),
                QImage::Format_ARGB32_Premultiplied)
    , m_overlay_painter(&m_overlay)
{
    ensure_tile_grid();
    clear_tile_geometry();
    m_overlay.fill(Qt::transparent);
    m_painter = &m_overlay_painter;
    m_overlay_painter.setAntialias(false);
    m_overlay_painter.setSmoothPixmap(false);
}

// ---- frame lifecycle -------------------------------------------------------

void rhi_renderer::begin_frame()
{
    ensure_tile_grid();
    clear_tile_geometry();
    m_palette_rgba.clear();
    m_palette_index.clear();
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

    // Keep the cached tile geometry intact; only overlay primitives should be
    // regenerated during camera-only updates.
    m_skip_tile_writes = true;
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

StyleIndex rhi_renderer::current_style_index()
{
    const std::uint32_t color = current_packed_color();
    auto it = m_palette_index.find(color);
    if (it != m_palette_index.end())
        return it->second;

    if (m_palette_rgba.size() >= kMaxRhiStyleEntries) {
        qFatal("rhi_renderer: palette exceeded %zu RGBA entries; compact style-index path exhausted",
               kMaxRhiStyleEntries);
    }

    const StyleIndex next = StyleIndex(m_palette_rgba.size());
    m_palette_rgba.push_back(color);
    m_palette_index.emplace(color, next);
    return next;
}

void rhi_renderer::append_segment(std::vector<PosVertex>&  verts,
                                  std::vector<StyleIndex>& styles,
                                  point2d                  start,
                                  point2d                  end,
                                  StyleIndex               style_index)
{
    verts.push_back(make_vertex(start));
    verts.push_back(make_vertex(end));
    styles.push_back(style_index);
    styles.push_back(style_index);
}

void rhi_renderer::append_fill_rect(RhiTileBatch& tile,
                                    point2d       p0,
                                    point2d       p1,
                                    StyleIndex    style_index)
{
    if (p1.x <= p0.x || p1.y <= p0.y)
        return;

    const point2d a{p0.x, p0.y};
    const point2d b{p1.x, p0.y};
    const point2d c{p0.x, p1.y};
    const point2d d{p1.x, p1.y};

    tile.fill_verts.push_back(make_vertex(a));
    tile.fill_verts.push_back(make_vertex(b));
    tile.fill_verts.push_back(make_vertex(c));

    tile.fill_verts.push_back(make_vertex(b));
    tile.fill_verts.push_back(make_vertex(d));
    tile.fill_verts.push_back(make_vertex(c));

    tile.fill_styles.insert(tile.fill_styles.end(), 6, style_index);
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
            m_tiles[std::size_t(tile_index(tx, ty))].world_bounds =
                rectangle{{left, bottom}, {right, top}};
        }
    }
}

void rhi_renderer::clear_tile_geometry()
{
    for (RhiTileBatch& tile : m_tiles) {
        tile.line_verts.clear();
        tile.line_styles.clear();
        tile.fill_verts.clear();
        tile.fill_styles.clear();
        tile.draw_verts.clear();
        tile.draw_styles.clear();
        tile.thick_line_instances.clear();
        tile.thick_line_styles.clear();
        tile.dashed_line_instances.clear();
        tile.dashed_line_styles.clear();
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

RhiTileBatch& rhi_renderer::tile_at(int tile_x, int tile_y)
{
    return m_tiles[std::size_t(tile_index(tile_x, tile_y))];
}

void rhi_renderer::append_line_to_tiles(point2d start,
                                        point2d end,
                                        StyleIndex style_index)
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
            append_segment(tile.line_verts,
                           tile.line_styles,
                           clipped_start,
                           clipped_end,
                           style_index);
        }
    }
}

void rhi_renderer::append_draw_segment_to_tiles(point2d start,
                                                point2d end,
                                                StyleIndex style_index)
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
            append_segment(tile.draw_verts,
                           tile.draw_styles,
                           clipped_start,
                           clipped_end,
                           style_index);
        }
    }
}

void rhi_renderer::append_fill_rect_to_tiles(point2d p0,
                                             point2d p1,
                                             StyleIndex style_index)
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
                             style_index);
        }
    }
}

// ---- thick line helpers ----------------------------------------------------

void rhi_renderer::append_thick_segment(RhiTileBatch& tile,
                                        point2d       start,
                                        point2d       end,
                                        float         width_px,
                                        StyleIndex    style_index)
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    if (std::sqrt(dx * dx + dy * dy) < 1e-10)
        return; // degenerate (zero-length) segment

    // One instance record (20 bytes) per segment.
    // The vertex shader reconstructs all 4 quad corners from this record plus
    // the constant 4-corner quad buffer — no per-vertex duplication of endpoints.
    tile.thick_line_instances.push_back({
        float(start.x), float(start.y),
        float(end.x),   float(end.y),
        width_px
    });
    tile.thick_line_styles.push_back(style_index); // 1 per instance, not per vertex
}

void rhi_renderer::append_thick_line_to_tiles(point2d    start,
                                              point2d    end,
                                              float      width_px,
                                              StyleIndex style_index)
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
                                 width_px,
                                 style_index);
        }
    }
}

void rhi_renderer::append_thick_draw_segment_to_tiles(point2d    start,
                                                      point2d    end,
                                                      float      width_px,
                                                      StyleIndex style_index)
{
    // Reuses the same geometry pool as thick draw_lines (same pipeline).
    append_thick_line_to_tiles(start, end, width_px, style_index);
}

// ---- dashed line helpers ---------------------------------------------------

void rhi_renderer::append_dashed_segment(RhiTileBatch& tile,
                                         point2d       start,
                                         point2d       end,
                                         float         width_px,
                                         float         dash_px,
                                         float         gap_px,
                                         float         phase_world,
                                         StyleIndex    style_index)
{
    const double dx = end.x - start.x;
    const double dy = end.y - start.y;
    if (std::sqrt(dx * dx + dy * dy) < 1e-10)
        return;

    tile.dashed_line_instances.push_back({
        float(start.x), float(start.y),
        float(end.x),   float(end.y),
        width_px, dash_px, gap_px, phase_world
    });
    tile.dashed_line_styles.push_back(style_index);
}

void rhi_renderer::append_dashed_line_to_tiles(point2d    start,
                                               point2d    end,
                                               float      width_px,
                                               float      dash_px,
                                               float      gap_px,
                                               StyleIndex style_index)
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
                                  width_px, dash_px, gap_px,
                                  phase_world, style_index);
        }
    }
}

void rhi_renderer::append_dashed_draw_segment_to_tiles(point2d    start,
                                                       point2d    end,
                                                       float      width_px,
                                                       float      dash_px,
                                                       float      gap_px,
                                                       StyleIndex style_index)
{
    append_dashed_line_to_tiles(start, end, width_px, dash_px, gap_px, style_index);
}

// Convert the active line dash mode to screen-pixel dash/gap lengths.
// Phase continuity across tile clipping is handled separately via phase_world.
void rhi_renderer::set_dash_pattern(float width_px,
                                    float& dash_px,
                                    float& gap_px) const
{
    switch (current_line_dash) {
        case ezgl::line_dash::none:
            dash_px = 0.0f;
            gap_px = 0.0f;
            return;
        case ezgl::line_dash::asymmetric_5_3:
            dash_px = 5.0f * width_px;
            gap_px  = 3.0f * width_px;
            return;
        default:
            dash_px = 5.0f * width_px;
            gap_px  = 3.0f * width_px;
            return;
    }
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
        // SCREEN mode: fall through to the QPainter overlay.
        renderer::draw_line(start, end);
        return;
    }
    if (m_skip_tile_writes)
        return;

    const StyleIndex style_index = current_style_index();

    if (current_line_dash != line_dash::none) {
        const float w = float(std::max(1, current_line_width));
        float dash_px = 0.0f;
        float gap_px = 0.0f;
        set_dash_pattern(w, dash_px, gap_px);
        append_dashed_line_to_tiles(start, end, w, dash_px, gap_px, style_index);
        return;
    }

    if (current_line_width > 1) {
        append_thick_line_to_tiles(start, end, float(current_line_width), style_index);
        return;
    }

    append_line_to_tiles(start, end, style_index);
}

// ---- fill_rectangle overrides ----------------------------------------------

void rhi_renderer::fill_rectangle(point2d start, point2d end)
{
    if (current_coordinate_system != WORLD) {
        renderer::fill_rectangle(start, end);
        return;
    }
    if (m_skip_tile_writes)
        return;

    const point2d p0{ std::min(start.x, end.x), std::min(start.y, end.y) };
    const point2d p1{ std::max(start.x, end.x), std::max(start.y, end.y) };
    append_fill_rect_to_tiles(p0, p1, current_style_index());
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
    if (m_skip_tile_writes)
        return;

    const point2d p0{ std::min(start.x, end.x), std::min(start.y, end.y) };
    const point2d p1{ std::max(start.x, end.x), std::max(start.y, end.y) };
    const StyleIndex style_index = current_style_index();

    if (current_line_dash != line_dash::none) {
        const float w = float(std::max(1, current_line_width));
        float dash_px = 0.0f;
        float gap_px = 0.0f;
        set_dash_pattern(w, dash_px, gap_px);
        append_dashed_draw_segment_to_tiles({p0.x, p0.y}, {p1.x, p0.y}, w, dash_px, gap_px, style_index);
        append_dashed_draw_segment_to_tiles({p1.x, p0.y}, {p1.x, p1.y}, w, dash_px, gap_px, style_index);
        append_dashed_draw_segment_to_tiles({p1.x, p1.y}, {p0.x, p1.y}, w, dash_px, gap_px, style_index);
        append_dashed_draw_segment_to_tiles({p0.x, p1.y}, {p0.x, p0.y}, w, dash_px, gap_px, style_index);
        return;
    }

    if (current_line_width > 1) {
        const float w = float(current_line_width);
        append_thick_draw_segment_to_tiles({p0.x, p0.y}, {p1.x, p0.y}, w, style_index);
        append_thick_draw_segment_to_tiles({p1.x, p0.y}, {p1.x, p1.y}, w, style_index);
        append_thick_draw_segment_to_tiles({p1.x, p1.y}, {p0.x, p1.y}, w, style_index);
        append_thick_draw_segment_to_tiles({p0.x, p1.y}, {p0.x, p0.y}, w, style_index);
        return;
    }

    append_draw_segment_to_tiles({p0.x, p0.y}, {p1.x, p0.y}, style_index);
    append_draw_segment_to_tiles({p1.x, p0.y}, {p1.x, p1.y}, style_index);
    append_draw_segment_to_tiles({p1.x, p1.y}, {p0.x, p1.y}, style_index);
    append_draw_segment_to_tiles({p0.x, p1.y}, {p0.x, p0.y}, style_index);
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

    std::vector<RhiTileBatch> non_empty_tiles;
    double total_mb = 0.0;
    for (RhiTileBatch& tile : m_tiles) {
        if (tile.empty())
            continue;

        total_mb +=
            ((tile.line_verts.size() +
              tile.fill_verts.size() +
              tile.draw_verts.size()) * sizeof(PosVertex)
             + tile.thick_line_instances.size()  * sizeof(ThickLineInstance)
             + tile.dashed_line_instances.size() * sizeof(DashedLineInstance)
             + (tile.line_styles.size() +
                tile.fill_styles.size() +
                tile.draw_styles.size() +
                tile.thick_line_styles.size() +
                tile.dashed_line_styles.size()) * sizeof(StyleIndex))
            / (1024.0 * 1024.0);
        non_empty_tiles.push_back(std::move(tile));
    }

    std::cout << "~~~ sending to GPU " << total_mb << " mb" << std::endl;

    m_rhi_widget->set_frame_data(
        std::move(non_empty_tiles),
        std::move(m_palette_rgba),
        compute_mvp(),
        get_visible_world(),
        m_overlay,
        m_bg_color);

    m_rhi_widget->update();
}

// ---- flush_mvp_only --------------------------------------------------------

void rhi_renderer::flush_mvp_only()
{
    if (!m_draw_callback) {
        // Fallback: if no draw callback is available, we can at least move the
        // existing geometry with the new MVP.
        m_rhi_widget->set_mvp_only(compute_mvp(), get_visible_world());
        m_rhi_widget->update();
        return;
    }

    begin_overlay_frame();
    m_draw_callback(this);

    m_skip_tile_writes = false;
    if (m_overlay_painter.isActive())
        m_overlay_painter.end();

    m_rhi_widget->set_mvp_and_overlay(compute_mvp(), get_visible_world(), m_overlay);
    m_rhi_widget->update();
}

} // namespace ezgl

#endif // EZGL_QT && EZGL_RHI
