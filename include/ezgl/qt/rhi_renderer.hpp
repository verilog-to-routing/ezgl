#pragma once

#ifdef EZGL_RHI

#include "ezgl/irenderer.hpp"
#include "ezgl/qt/deferred_renderer.hpp"
#include "ezgl/qt/rhi_canvas_widget.hpp"

#include <QMatrix4x4>
#include <QImage>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ezgl {

/**
 * GPU-backed renderer with scene tiling.
 *
 * Hot-path primitives are clipped into a fixed 256 x 256 grid over the scene
 * bounds while being grouped by style. flush() repacks the occupied tile
 * batches into scene-wide style buffers with chunk bounds for GPU-side upload.
 *
 * Overlay primitives (text, arcs, surfaces, SCREEN-space lines, …) are cached
 * through a owned deferred_renderer (m_overlay_deferred) so they can be
 * replayed into m_overlay when the camera changes without re-running the
 * application draw callback.
 */
class rhi_renderer : public irenderer {
public:
    using draw_callback_fn = void (*)(renderer*);

    rhi_renderer(RhiCanvasWidget* widget,
                 transform_fn     transform,
                 camera*          cam,
                 draw_callback_fn draw_callback,
                 QColor           bg_color);

    ~rhi_renderer() = default;

    // ---- irenderer: coordinate system / viewport ---------------------------

    void set_coordinate_system(t_coordinate_system cs) override;
    void set_visible_world(rectangle new_world) override;
    rectangle get_visible_world() override;
    rectangle get_visible_screen() const override;
    rectangle world_to_screen(const rectangle& box) override;

    // ---- irenderer: state setters ------------------------------------------

    void set_color(color c) override;
    void set_color(color c, uint_fast8_t alpha) override;
    void set_color(uint_fast8_t r, uint_fast8_t g, uint_fast8_t b,
                   uint_fast8_t a = 255) override;
    void set_line_cap(line_cap cap) override;
    void set_line_dash(line_dash dash) override;
    void set_line_width(int width) override;
    void set_font_size(double size) override;
    void format_font(std::string const& family, font_slant slant,
                     font_weight weight) override;
    void format_font(std::string const& family, font_slant slant,
                     font_weight weight, double new_size) override;
    void set_text_rotation(double degrees) override;
    void set_horiz_justification(justification j) override;
    void set_vert_justification(justification j) override;

    // ---- irenderer: hot-path GPU draw calls --------------------------------

    void draw_line(const point2d& start, const point2d& end) override;

    void fill_rectangle(const point2d& start, const point2d& end) override;
    void fill_rectangle(const point2d& start, double width, double height) override;
    void fill_rectangle(rectangle r) override;

    void draw_rectangle(const point2d& start, const point2d& end) override;
    void draw_rectangle(const point2d& start, double width, double height) override;
    void draw_rectangle(rectangle r) override;

    // ---- irenderer: overlay draw calls (forwarded to m_overlay_deferred) ---

    void fill_poly(std::vector<point2d> const& points) override;
    void draw_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    void draw_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;
    void fill_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    void fill_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;
    void draw_text(const point2d& point, std::string const& text) override;
    void draw_text(const point2d& point, std::string const& text,
                   double bound_x, double bound_y) override;
    void draw_surface(surface* p_surface, const point2d& anchor_point,
                      double scale_factor = 1) override;

    // ---- Frame lifecycle ---------------------------------------------------

    /** Reset per-frame state (vertex buffers, overlay) ready for a new draw. */
    void begin_frame();

    /**
     * Transfer all collected geometry to RhiCanvasWidget and schedule repaint.
     * Also ends the overlay painter so the QImage is fully flushed.
     */
    void flush();

    /**
     * Rebuild the cached overlay for the current camera and update the MVP
     * without re-running the application draw callback.
     */
    void flush_mvp_only();

private:
    static constexpr int kTileGridDimension = 16;

    struct TileThinLineBatch {
        StyleKey               style_key = 0;
        std::uint32_t          rgba = 0;
        std::vector<PosVertex> verts;
    };

    struct TileFillRectBatch {
        StyleKey                    style_key = 0;
        std::uint32_t               rgba = 0;
        std::vector<FillRectInstance> instances;
    };

    struct TileFillPolyBatch {
        StyleKey               style_key = 0;
        std::uint32_t          rgba = 0;
        std::vector<PosVertex> verts;
    };

    struct TileThickLineBatch {
        StyleKey                      style_key = 0;
        std::uint32_t                 rgba = 0;
        std::vector<ThickLineInstance> instances;
    };

    struct TileDashedLineBatch {
        StyleKey                       style_key = 0;
        std::uint32_t                  rgba = 0;
        std::vector<DashedLineInstance> instances;
    };

    struct RhiTileBatch {
        rectangle                         world_bounds;
        std::uint16_t                     tile_x = 0;
        std::uint16_t                     tile_y = 0;
        std::vector<TileThinLineBatch>    thin_line_batches;
        std::vector<TileFillRectBatch>    fill_rect_batches;
        std::vector<TileFillPolyBatch>    fill_poly_batches;
        std::vector<TileThickLineBatch>   thick_line_batches;
        std::vector<TileDashedLineBatch>  dashed_line_batches;

        bool empty() const
        {
            return thin_line_batches.empty()
                && fill_rect_batches.empty()
                && fill_poly_batches.empty()
                && thick_line_batches.empty()
                && dashed_line_batches.empty();
        }
    };

    // ---- helpers ------------------------------------------------------------

    std::uint32_t current_packed_color() const;
    StyleKey current_style_key(PrimitiveType primitive_type,
                               float         line_width_px = 0.0f) const;
    TileThinLineBatch& ensure_thin_line_batch(RhiTileBatch& tile,
                                              StyleKey     style_key,
                                              std::uint32_t rgba);
    TileFillRectBatch& ensure_fill_rect_batch(RhiTileBatch& tile,
                                              StyleKey     style_key,
                                              std::uint32_t rgba);
    TileFillPolyBatch& ensure_fill_poly_batch(RhiTileBatch& tile,
                                              StyleKey     style_key,
                                              std::uint32_t rgba);
    TileThickLineBatch& ensure_thick_line_batch(RhiTileBatch& tile,
                                                StyleKey     style_key,
                                                std::uint32_t rgba);
    TileDashedLineBatch& ensure_dashed_line_batch(RhiTileBatch& tile,
                                                  StyleKey     style_key,
                                                  std::uint32_t rgba);
    void append_thin_line_segment(RhiTileBatch& tile,
                                  const point2d& start,
                                  const point2d& end,
                                  StyleKey      style_key,
                                  std::uint32_t rgba);
    void append_fill_rect(RhiTileBatch& tile,
                          const point2d& p0,
                          const point2d& p1,
                          StyleKey      style_key,
                          std::uint32_t rgba);
    void append_fill_triangle(RhiTileBatch& tile,
                              const point2d& a,
                              const point2d& b,
                              const point2d& c,
                              StyleKey      style_key,
                              std::uint32_t rgba);
    void append_line_to_tiles(const point2d& start,
                              const point2d& end,
                              StyleKey style_key,
                              std::uint32_t rgba);
    void append_fill_rect_to_tiles(const point2d& p0,
                                   const point2d& p1,
                                   StyleKey style_key,
                                   std::uint32_t rgba);
    void append_fill_triangle_to_tiles(const point2d& a,
                                       const point2d& b,
                                       const point2d& c,
                                       StyleKey   style_key,
                                       std::uint32_t rgba);

    void append_thick_segment(RhiTileBatch& tile,
                              const point2d& start,
                              const point2d& end,
                              StyleKey      style_key,
                              std::uint32_t rgba);
    void append_thick_line_to_tiles(const point2d& start,
                                    const point2d& end,
                                    StyleKey   style_key,
                                    std::uint32_t rgba);
    void append_thick_draw_segment_to_tiles(const point2d& start,
                                            const point2d& end,
                                            StyleKey   style_key,
                                            std::uint32_t rgba);

    void append_dashed_segment(RhiTileBatch& tile,
                               const point2d& start,
                               const point2d& end,
                               float         phase_world,
                               StyleKey      style_key,
                               std::uint32_t rgba);
    void append_dashed_line_to_tiles(const point2d& start,
                                     const point2d& end,
                                     StyleKey   style_key,
                                     std::uint32_t rgba);
    void append_dashed_draw_segment_to_tiles(const point2d& start,
                                             const point2d& end,
                                             StyleKey   style_key,
                                             std::uint32_t rgba);
    void begin_overlay_frame();
    void render_cached_overlay();
    void ensure_tile_grid();
    void clear_tile_geometry();
    int clamp_tile_x(double x) const;
    int clamp_tile_y(double y) const;
    int tile_index(int tile_x, int tile_y) const;
    RhiTileBatch& tile_at(int tile_x, int tile_y);
    SceneBuffers build_scene_buffers() const;

    /** Compute screen→NDC orthographic matrix from current widget size. */
    QMatrix4x4 compute_mvp() const;

    // ---- state --------------------------------------------------------------

    RhiCanvasWidget*         m_rhi_widget;
    QColor                   m_bg_color;
    bool                     m_skip_tile_writes = false;

    // Scene tiling metadata and CPU-side tile batches.
    rectangle                m_scene_bounds;
    double                   m_tile_width = 1.0;
    double                   m_tile_height = 1.0;
    std::vector<RhiTileBatch> m_tiles;

    // QPainter overlay — overlay commands (text, arcs, …) are stored in
    // m_overlay_deferred and replayed into this image.
    QImage   m_overlay;
    Painter  m_overlay_painter;   // must be declared AFTER m_overlay

    // Overlay command storage and replay — independent deferred renderer
    // painted into m_overlay_painter / m_overlay.
    std::unique_ptr<deferred_renderer> m_overlay_deferred;
};

} // namespace ezgl

#endif // EZGL_RHI
