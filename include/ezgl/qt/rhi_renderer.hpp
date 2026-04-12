#pragma once

#if defined(EZGL_QT) && defined(EZGL_RHI)

#include "ezgl/qt/deferred_renderer.hpp"
#include "ezgl/qt/rhi_canvas_widget.hpp"

#include <QMatrix4x4>
#include <QImage>
#include <cstddef>
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
 * through deferred_renderer so they can be replayed into m_overlay when the
 * camera changes without re-running the application draw callback.
 */
class rhi_renderer : public deferred_renderer {
public:
    using draw_callback_fn = void (*)(renderer*);

    rhi_renderer(RhiCanvasWidget* widget,
                 transform_fn     transform,
                 camera*          cam,
                 draw_callback_fn draw_callback,
                 QColor           bg_color);

    ~rhi_renderer() = default;

    // ---- Frame lifecycle ---------------------------------------------------

    /** Reset per-frame state (vertex buffers, overlay) ready for a new draw. */
    void begin_frame();

    // ---- Hot-path overrides -------------------------------------------------

    void draw_line(point2d start, point2d end) override;

    void fill_rectangle(point2d start, point2d end) override;
    void fill_rectangle(point2d start, double width, double height) override;
    void fill_rectangle(rectangle r) override;

    void draw_rectangle(point2d start, point2d end) override;
    void draw_rectangle(point2d start, double width, double height) override;
    void draw_rectangle(rectangle r) override;

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

protected:
    bool defer_fill_poly(const std::vector<point2d>& points) override;

private:
    static constexpr int kTileGridDimension = 256;

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

    PosVertex make_vertex(point2d world_pt) const;
    std::uint32_t current_packed_color() const;
    StyleKey current_style_key(PrimitiveType primitive_type,
                               float         line_width_px = 0.0f,
                               float         dash_px = 0.0f,
                               float         gap_px = 0.0f) const;
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
                                  point2d       start,
                                  point2d       end,
                                  StyleKey      style_key,
                                  std::uint32_t rgba);
    void append_fill_rect(RhiTileBatch& tile,
                          point2d       p0,
                          point2d       p1,
                          StyleKey      style_key,
                          std::uint32_t rgba);
    void append_fill_triangle(RhiTileBatch& tile,
                              point2d       a,
                              point2d       b,
                              point2d       c,
                              StyleKey      style_key,
                              std::uint32_t rgba);
    void append_line_to_tiles(point2d start,
                              point2d end,
                              StyleKey style_key,
                              std::uint32_t rgba);
    void append_fill_rect_to_tiles(point2d p0,
                                   point2d p1,
                                   StyleKey style_key,
                                   std::uint32_t rgba);
    void append_fill_triangle_to_tiles(point2d    a,
                                       point2d    b,
                                       point2d    c,
                                       StyleKey   style_key,
                                       std::uint32_t rgba);

    // Thick line helpers (line_width > 0).
    // Appends one ThickLineInstance per clipped segment (instanced rendering).
    void append_thick_segment(RhiTileBatch& tile,
                              point2d       start,
                              point2d       end,
                              float         width_px,
                              StyleKey      style_key,
                              std::uint32_t rgba);
    void append_thick_line_to_tiles(point2d    start,
                                    point2d    end,
                                    float      width_px,
                                    StyleKey   style_key,
                                    std::uint32_t rgba);
    void append_thick_draw_segment_to_tiles(point2d    start,
                                            point2d    end,
                                            float      width_px,
                                            StyleKey   style_key,
                                            std::uint32_t rgba);

    // Dashed line helpers.
    // Appends one DashedLineInstance per clipped segment.
    void append_dashed_segment(RhiTileBatch& tile,
                               point2d       start,
                               point2d       end,
                               float         width_px,
                               float         dash_px,
                               float         gap_px,
                               float         phase_world,
                               StyleKey      style_key,
                               std::uint32_t rgba);
    void append_dashed_line_to_tiles(point2d    start,
                                     point2d    end,
                                     float      width_px,
                                     float      dash_px,
                                     float      gap_px,
                                     StyleKey   style_key,
                                     std::uint32_t rgba);
    void append_dashed_draw_segment_to_tiles(point2d    start,
                                             point2d    end,
                                             float      width_px,
                                             float      dash_px,
                                             float      gap_px,
                                             StyleKey   style_key,
                                             std::uint32_t rgba);
    void set_dash_pattern(float width_px,
                          float& dash_px,
                          float& gap_px) const;
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

    // QPainter overlay — base-class draw calls (text, arcs, …) write here.
    QImage   m_overlay;
    Painter  m_overlay_painter;   // must be declared AFTER m_overlay
};

} // namespace ezgl

#endif // EZGL_QT && EZGL_RHI
