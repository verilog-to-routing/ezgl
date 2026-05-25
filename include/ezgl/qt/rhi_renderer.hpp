#pragma once

#include "ezgl/irenderer.hpp"
#include "ezgl/qt/deferred_renderer.hpp"
#include "ezgl/qt/rhi_types.hpp"
#include "ezgl/qt/rhi_canvas_widget.hpp"

#include <QMatrix4x4>
#include <QImage>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <vector>

namespace ezgl {

/**
 * @brief GPU-backed @ref irenderer implementation. The recording side of
 * the rhi backend.
 *
 * Receives the application's draw callbacks (one method per primitive),
 * bins each primitive into a fixed 32x32 grid (@ref kTileGridDimension) of
 * per-style batches over the scene bounds, then on @ref flush() repacks the
 * occupied tile batches into scene-wide @ref ezgl::SceneBuffers with a
 * per-tile @ref ezgl::Chunk for GPU-side viewport culling. The rebuilt
 * scene is handed to @ref RhiCanvasWidget which forwards it to the render
 * thread.
 *
 * @par How chunks are built (two-stage: record then assemble)
 * During the user's draw callback (record stage), each primitive is
 * clipped to the tile grid and appended into per-tile batches
 * (@c RhiTileBatch in @c m_tiles), grouped by @ref ezgl::StyleKey
 * within each tile via linear lookup over the tile's style-batch list.
 * At @ref flush() time, @c build_scene_buffers walks tiles in
 * tile-grid order and copies the per-tile batches into scene-wide
 * @ref ezgl::SceneBuffers, emitting **one @ref ezgl::Chunk per (tile,
 * style) pair** with @c (offset, count) recording that pair's slice
 * inside the scene-wide flat array. The repack is deterministic
 * (driven by tile traversal order) so no sort pass is needed, but the
 * data is copied — record-time batches are intermediate, not the final
 * GPU-bound buffers.
 *
 * @par GPU vs CPU primitives
 * The following primitives are GPU-rendered through one of the six geometry
 * pipelines in @ref RhiSceneRenderer: @c draw_line, @c fill_rectangle,
 * @c draw_rectangle (decomposed into 4 thin/thick lines or one fill_rect
 * instance depending on style), plus @c fill_triangle / @c fill_poly via
 * the fill_poly pipeline and the GPU arrow pipeline for
 * @c fill_arrow_pointer_triangle. All other primitives — @c draw_text,
 * @c draw_arc / @c fill_arc (and their elliptic variants), @c draw_surface,
 * SCREEN-coordinate-system overrides — forward to an owned
 * @ref deferred_renderer (@ref m_overlay_deferred) painting into the
 * @ref m_overlay QImage. That QImage is uploaded as a GPU texture and
 * composited on top of the GPU layers by the overlay pipeline.
 *
 * @par Parallel record
 * Per-primitive-type command vectors are sharded across N bands of tile
 * rows where @c N = @c std::thread::hardware_concurrency(). Each band's
 * dispatch only touches the tiles in its row range so tile-state updates
 * are contention-free. See @c m_n_bands, @c m_rows_per_band.
 *
 * @par Camera-only redraws
 * On pan/zoom with no scene change, @ref flush_mvp_only() re-runs the
 * overlay callbacks (text/arc bounds depend on screen-space layout) but
 * leaves the GPU scene buffers untouched. The widget receives just a new
 * MVP + overlay image. The big win versus the deferred backend is here.
 *
 * @par Headless mode
 * The second constructor takes a @c QSize instead of a widget, and
 * @ref flush_capture() returns the assembled frame data as a
 * @ref HeadlessFrameData by value (no widget, no GPU dependency at this
 * level). @ref rhi_backend::render_to_image() uses this with
 * @ref RhiCanvasWidget::render_offscreen() to back @c save_graphics().
 *
 * @see RhiCanvasWidget for the Qt-side widget + thread inbox.
 * @see RhiSceneRenderer for the GPU pipeline + frame-slot resources.
 * @see rhi_backend for the lifecycle wrapper.
 */
class rhi_renderer : public irenderer {
public:
    using draw_callback_fn = void (*)(renderer*);

    /// Data captured from a single headless frame — returned by flush_capture()
    /// so the caller can render it via RhiCanvasWidget::render_offscreen() without
    /// needing a live QRhiWidget or QRhiWidget::grab().
    struct HeadlessFrameData {
        SceneBuffers scene;
        QMatrix4x4   mvp;
        rectangle    visible_world;
        QImage       overlay;
        QColor       bg;
    };

    /**
     * Display constructor: bound to a live @ref RhiCanvasWidget. The
     * widget's current size and devicePixelRatio define the overlay
     * QImage resolution. @ref flush() pushes frame data into the widget's
     * thread-safe inbox.
     */
    rhi_renderer(RhiCanvasWidget* widget,
                 transform_fn     transform,
                 camera*          cam,
                 draw_callback_fn draw_callback,
                 QColor           bg_color);

    /**
     * Headless constructor: no widget, explicit pixel size, no device
     * pixel ratio scaling. Pairs with @ref flush_capture() and
     * @ref RhiCanvasWidget::render_offscreen() to render scenes outside
     * the Qt widget lifecycle (e.g. @c save_graphics under headless QPA).
     */
    rhi_renderer(QSize            size,
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
    void set_text_screen_offset(point2d offset_px) override;

    // ---- irenderer: hot-path GPU draw calls --------------------------------

    void draw_line(const point2d& start, const point2d& end) override;

    void fill_rectangle(const point2d& start, const point2d& end) override;
    void fill_rectangle(const point2d& start, double width, double height) override;
    void fill_rectangle(const rectangle& r) override;

    void draw_rectangle(const point2d& start, const point2d& end) override;
    void draw_rectangle(const point2d& start, double width, double height) override;
    void draw_rectangle(const rectangle& r) override;

    // ---- irenderer: overlay draw calls (forwarded to m_overlay_deferred) ---

    void fill_poly(const std::vector<point2d>& points) override;
    void fill_triangle(const point2d& a, const point2d& b, const point2d& c) override;
    void fill_arrow_pointer_triangle(const point2d& anchor_world,
                                      const point2d& dir_world,
                                      float          arrow_size_px) override;
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
    //
    // Typical full-redraw cycle:
    //   begin_frame();
    //   <user draw callback emits primitives via the irenderer methods above>
    //   flush();                         // → widget
    //
    // Camera-only redraw (pan/zoom, no scene change):
    //   flush_mvp_only();                // overlay rebuilt; GPU scene untouched
    //
    // Headless save_graphics:
    //   begin_frame();
    //   <draw callback>
    //   auto data = flush_capture(bg);   // returns by value, no widget
    //   QImage png = RhiCanvasWidget::render_offscreen(w, h, ...data...);

    /// Reset per-frame state (tile batches, command vectors, overlay)
    /// ready for a fresh recording pass.
    void begin_frame();

    /// Repack tile batches into @ref SceneBuffers, push frame data into
    /// the bound @ref RhiCanvasWidget, and schedule a repaint. Also ends
    /// the overlay painter so the QImage is fully flushed to bytes.
    void flush();

    /// Headless variant of @ref flush(): dispatches commands to tiles,
    /// builds @ref SceneBuffers, captures the overlay image, and returns
    /// the assembled frame data without touching any widget. Used by
    /// @c rhi_backend::render_to_image() to feed
    /// @ref RhiCanvasWidget::render_offscreen().
    HeadlessFrameData flush_capture(const QColor& bg);

    /// Rebuild the overlay layer (text/arcs have screen-relative layout)
    /// for the current camera and push a new MVP without re-running the
    /// application draw callback or rebuilding any GPU scene buffers.
    void flush_mvp_only();

private:
    static constexpr int kTileGridDimension  = 32;
    static constexpr int kBatchInitialReserve = 1024;

    struct TileThinLineBatch {
        StyleKey               style_key = 0;
        std::uint32_t          rgba = 0;
        std::vector<PosVertex> verts;
        TileThinLineBatch(StyleKey sk, std::uint32_t c) : style_key(sk), rgba(c) {}
    };

    struct TileFillRectBatch {
        StyleKey                      style_key = 0;
        std::uint32_t                 rgba = 0;
        std::vector<FillRectInstance> instances;
        TileFillRectBatch(StyleKey sk, std::uint32_t c) : style_key(sk), rgba(c) {}
    };

    struct TileFillPolyBatch {
        StyleKey               style_key = 0;
        std::uint32_t          rgba = 0;
        std::vector<PosVertex> verts;
        TileFillPolyBatch(StyleKey sk, std::uint32_t c) : style_key(sk), rgba(c) {}
    };

    struct TileThickLineBatch {
        StyleKey                       style_key = 0;
        std::uint32_t                  rgba = 0;
        std::vector<ThickLineInstance> instances;
        TileThickLineBatch(StyleKey sk, std::uint32_t c) : style_key(sk), rgba(c) {}
    };

    struct TileDashedLineBatch {
        StyleKey                        style_key = 0;
        std::uint32_t                   rgba = 0;
        std::vector<DashedLineInstance> instances;
        TileDashedLineBatch(StyleKey sk, std::uint32_t c) : style_key(sk), rgba(c) {}
    };

    struct RhiTileBatch {
        RhiTileBatch() {
            thin_line_batches.reserve(kBatchInitialReserve);
            fill_rect_batches.reserve(kBatchInitialReserve);
            fill_poly_batches.reserve(kBatchInitialReserve);
            thick_line_batches.reserve(kBatchInitialReserve);
            dashed_line_batches.reserve(kBatchInitialReserve);
        }

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
    void clear_commands();
    void dispatch_commands_to_tiles(int band);
    int  band_for_tile_row(int ty) const { return std::min(ty / m_rows_per_band, m_n_bands - 1); }
    int  band_ty_min(int band) const { return band * m_rows_per_band; }
    int  band_ty_max(int band) const { return std::min((band + 1) * m_rows_per_band - 1, kTileGridDimension - 1); }
    int clamp_tile_x(double x) const;
    int clamp_tile_y(double y) const;
    int tile_index(int tile_x, int tile_y) const;
    RhiTileBatch& tile_at(int tile_x, int tile_y);
    SceneBuffers build_scene_buffers() const;

    /** Compute screen→NDC orthographic matrix from current widget size. */
    QMatrix4x4 compute_mvp() const;

    // ---- state --------------------------------------------------------------

    RhiCanvasWidget*         m_rhi_widget; ///< null in headless mode
    QSize                    m_size;       ///< logical (device-independent) framebuffer size
    qreal                    m_overlay_dpr = 1.0; ///< overlay QImage device pixel ratio (matches widget DPR; 1.0 headless)
    QColor                   m_bg_color;
    bool                     m_skip_tile_writes = false;
    std::uint32_t            m_current_rgba = 0;

    // Scene tiling metadata and CPU-side tile batches.
    rectangle                m_scene_bounds;
    double                   m_tile_width = 1.0;
    double                   m_tile_height = 1.0;
    std::vector<RhiTileBatch> m_tiles;

    // ---- draw command recording (filled during draw callback) ---------------
    // Commands are routed at record time into per-band buckets so each
    // dispatch thread only iterates the commands that touch its tile rows.
    // rgba is stored in the lower 32 bits of sk (see pack_style_key).

    struct ThinLineCmd   { StyleKey sk; float x0, y0, x1, y1; };
    struct FillRectCmd   { StyleKey sk; float x0, y0, x1, y1; };
    struct FillTriCmd    { StyleKey sk; float x0, y0, x1, y1, x2, y2; };
    struct ThickLineCmd  { StyleKey sk; float x0, y0, x1, y1; };
    struct DashedLineCmd { StyleKey sk; float x0, y0, x1, y1; };
    struct ArrowCmd      { StyleKey sk; float ax, ay, dx, dy; };

    int m_n_bands       = 1;
    int m_rows_per_band = kTileGridDimension;

    std::vector<std::vector<ThinLineCmd>>   m_cmd_thin_lines;
    std::vector<std::vector<FillRectCmd>>   m_cmd_fill_rects;
    std::vector<std::vector<FillTriCmd>>    m_cmd_fill_tris;
    std::vector<std::vector<ThickLineCmd>>  m_cmd_thick_lines;
    std::vector<std::vector<DashedLineCmd>> m_cmd_dashed_lines;

    // Arrows are not tile-binned: their on-screen extent is small (a few
    // pixels) and screen-space culling would require knowing the camera
    // at record time, which differs between record and replay under the
    // camera-only redraw path. The GPU draws every recorded instance.
    std::vector<ArrowCmd>                   m_cmd_arrows;

    // QPainter overlay — overlay commands (text, arcs, …) are stored in
    // m_overlay_deferred and replayed into this image.
    QImage   m_overlay;
    Painter  m_overlay_painter;   // must be declared AFTER m_overlay

    // Overlay command storage and replay — independent deferred renderer
    // painted into m_overlay_painter / m_overlay.
    std::unique_ptr<deferred_renderer> m_overlay_deferred;
};

} // namespace ezgl
