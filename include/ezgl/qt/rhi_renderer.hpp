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
 * @par Why chunk order must stay deterministic
 * Chunk order is draw order, and the geometry pipelines blend with depth
 * testing off — a painter's algorithm, so the last draw wins the pixel and
 * translucent overlaps change colour outright (alpha blending is not
 * commutative). This is why the per-tile style lists are vectors scanned
 * linearly rather than hash maps: iterating an @c unordered_map would tie the
 * order to hash order, which varies by run and platform, so the same scene
 * could render differently and @c save_graphics() would stop being
 * reproducible. (@ref deferred_renderer does use an @c unordered_map, but only
 * to look up a batch's index; it draws by iterating its batch vector, so the
 * map's order never reaches the output. Here the map would hold the batches
 * themselves, making its order the draw order.)
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
 * level). @ref rhi_backend::render_to_image() uses this with the *static*
 * @ref RhiCanvasWidget::render_offscreen() — no widget is instantiated — to
 * back @c save_graphics().
 *
 * @see RhiCanvasWidget for the Qt-side widget + thread inbox.
 * @see RhiSceneRenderer for the GPU pipeline + frame-slot resources.
 * @see rhi_backend for the lifecycle wrapper.
 *
 * @note New to the graphics acronyms below (UBO, VBO, MSAA, NDC, …)? They are
 *       defined once in the glossary at the top of @ref rhi_types.hpp.
 */
class rhi_renderer : public irenderer {
public:
    /// User draw routine invoked once per full flush. It receives this
    /// renderer (as the base @ref renderer interface) and issues @c draw_* /
    /// @c fill_* calls, which the renderer records into @ref SceneBuffers for
    /// GPU upload rather than painting immediately.
    using draw_callback_fn = void (*)(renderer*);

    /// Data captured from a single headless frame — returned by flush_capture()
    /// so the caller can render it via RhiCanvasWidget::render_offscreen() without
    /// needing a live QRhiWidget or QRhiWidget::grab().
    struct HeadlessFrameData {
        SceneBuffers scene;         ///< CPU-side geometry to upload and draw on the offscreen GPU.
        QMatrix4x4   mvp;           ///< World→clip transform for this frame.
        rectangle    visible_world; ///< Visible world rect, used for chunk culling during the offscreen draw.
        QImage       overlay;       ///< Pre-rendered text/surface overlay composited over the GPU frame.
        QColor       bg;            ///< Background clear color.
    };

    /**
     * Display constructor: bound to a live @ref RhiCanvasWidget. The
     * widget's current size and devicePixelRatio define the overlay
     * QImage resolution. @ref flush() pushes frame data into the widget's
     * thread-safe inbox.
     */
    rhi_renderer(RhiCanvasWidget* widget,
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
                 camera*          cam,
                 draw_callback_fn draw_callback,
                 QColor           bg_color);

    /// Default: the owned overlay deferred renderer and GPU scene state are
    /// freed by their own destructors. The widget and camera are non-owning.
    ~rhi_renderer() = default;

    // ---- irenderer: coordinate system / viewport ---------------------------
    //
    // Interface contract is documented on the @ref irenderer base; the notes
    // below add only rhi-specific behaviour. Undocumented overrides inherit
    // the base doc verbatim.

    void set_coordinate_system(t_coordinate_system cs) override;
    /// In addition to the base contract: drives the MVP and the @ref Chunk
    /// visibility test used to cull non-visible geometry each frame.
    void set_visible_world(rectangle new_world) override;
    rectangle get_visible_world() override;
    rectangle get_visible_screen() const override;
    rectangle world_to_screen(const rectangle& box) override;

    // ---- irenderer: state setters ------------------------------------------
    //
    // The active color / line width / dash fold into the @ref StyleKey of
    // every primitive emitted afterwards; font and text state is forwarded to
    // the overlay renderer instead. Per-method contracts are on @ref irenderer.

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
    //
    // These bin geometry into the per-tile batches keyed by the current
    // @ref StyleKey; nothing is rasterised until @ref flush(). Per-method
    // contracts are on @ref irenderer.

    /// Record a line into the thin- or thick-line tile batch (chosen by the
    /// current line width), or the dashed batch when a dash style is active.
    void draw_line(const point2d& start, const point2d& end) override;

    /// @name Record a filled rectangle into the fill-rect tile batch.
    /// @{
    void fill_rectangle(const point2d& start, const point2d& end) override;
    void fill_rectangle(const point2d& start, double width, double height) override;
    void fill_rectangle(const rectangle& r) override;
    /// @}

    /// @name Record a rectangle outline as four lines into the line tile batch.
    /// @{
    void draw_rectangle(const point2d& start, const point2d& end) override;
    void draw_rectangle(const point2d& start, double width, double height) override;
    void draw_rectangle(const rectangle& r) override;
    /// @}

    // ---- irenderer: polygon / arrow / overlay draw calls -------------------
    //
    // Mixed routing: filled polygons/triangles go to the GPU FilledPoly batch
    // in WORLD space but are forwarded to the overlay renderer in SCREEN
    // space; arrows are always GPU; arcs, text, and surfaces are always
    // forwarded to the overlay (composited as a QImage over the GPU frame).
    // Per-method contracts are on @ref irenderer.

    /// In WORLD space: triangulate and record into the GPU @ref
    /// PrimitiveType::FilledPoly tile batch. In SCREEN space: forward to the
    /// overlay renderer.
    void fill_poly(const std::vector<point2d>& points) override;
    /// In WORLD space: record one triangle into the GPU @ref
    /// PrimitiveType::FilledPoly tile batch. In SCREEN space: forward to the
    /// overlay renderer.
    void fill_triangle(const point2d& a, const point2d& b, const point2d& c) override;
    /// Always GPU: push one @ref PrimitiveType::Arrow instance whose vertex
    /// shader synthesises a constant-pixel-size triangle; @c arrow_size_px is
    /// packed into the @ref StyleKey line-width slot.
    /// Unlike @ref fill_triangle above, this ignores the current coordinate
    /// system — the anchor is always taken as WORLD (the size is already in
    /// pixels, which is the point of the call). Setting SCREEN beforehand does
    /// not reinterpret the anchor as a pixel position.
    void fill_arrow_pointer_triangle(const point2d& anchor_world,
                                      const point2d& dir_world,
                                      float          arrow_size_px) override;
    /// @name Forward an arc outline / fill to the overlay renderer.
    /// @{
    void draw_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    void draw_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;
    void fill_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    void fill_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;
    /// @}
    /// @name Forward a text label to the overlay renderer.
    /// @{
    void draw_text(const point2d& point, std::string const& text) override;
    void draw_text(const point2d& point, std::string const& text,
                   double bound_x, double bound_y) override;
    /// @}
    /// Forward an image blit to the overlay renderer.
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
    /// @c rhi_backend::render_to_image() to feed the *static*
    /// @ref RhiCanvasWidget::render_offscreen(), which likewise needs no
    /// widget instance.
    HeadlessFrameData flush_capture(const QColor& bg);

    /// Rebuild the overlay layer (text/arcs have screen-relative layout)
    /// for the current camera and push a new MVP without re-running the
    /// application draw callback or rebuilding any GPU scene buffers.
    ///
    /// What gets re-uploaded on this path: just @c mvp_ubuf (80 B) plus
    /// the overlay QImage as a texture (because text/arc bounds depend
    /// on the camera). Scene VBOs are untouched in VRAM. The cheapness
    /// of camera-only pan/zoom relies on @ref RhiSceneRenderer::render()
    /// re-evaluating @ref ezgl::Chunk visibility against the new
    /// @c visible_world on every frame (not only on @c geom_dirty), so
    /// the freshly-pannned viewport's draw call set is recomputed
    /// without rebuilding any geometry.
    void flush_mvp_only();

private:
    /// Side length, in tiles, of the square screen-space tiling grid. The scene
    /// is binned into @c kTileGridDimension × @c kTileGridDimension tiles so that
    /// each tile+style pair becomes one culled @ref Chunk at @ref flush().
    static constexpr int kTileGridDimension  = 32;
    /// Initial capacity reserved for each per-type tile batch vector, sized to
    /// absorb a typical tile's primitive count without reallocation churn while
    /// the draw callback records geometry.
    static constexpr int kBatchInitialReserve = 1024;

    // ---- Tile batches (per-tile, per-style geometry being recorded) --------
    //
    // While the draw callback runs, geometry is accumulated per screen-space
    // tile and, within each tile, grouped by @ref StyleKey so that one tile +
    // one style maps to one contiguous draw range. At @ref flush() the tiles
    // are concatenated per style into the @ref SceneBuffers chunks uploaded to
    // the GPU. Each batch caches the unpacked @c rgba alongside its
    // @c style_key to avoid re-decoding the key.
    //
    // There is deliberately no arrow tile batch. Tiling exists so each chunk
    // can carry a static, zoom-invariant world-space AABB that the per-chunk
    // visibility test culls against. An arrow has no such bound: its anchor is
    // a world point, but the triangle is expanded to a fixed *pixel* size in
    // the vertex shader, so its world-space footprint grows and shrinks with
    // zoom and is unknown at record time. With no zoom-stable extent there is
    // no tile it can be correctly assigned to, so arrows bypass tiling and are
    // recorded directly in @c m_cmd_arrows; at build time each style group is
    // emitted as a single scene-wide chunk that always passes the visibility
    // test.

    /// One tile's worth of thin (1-pixel) lines sharing one @ref StyleKey.
    struct TileThinLineBatch {
        StyleKey               style_key = 0;
        std::uint32_t          rgba = 0;
        std::vector<PosVertex> verts;
        TileThinLineBatch(StyleKey sk, std::uint32_t c) : style_key(sk), rgba(c) {}
    };

    /// One tile's worth of filled rectangles sharing one @ref StyleKey.
    struct TileFillRectBatch {
        StyleKey                      style_key = 0;
        std::uint32_t                 rgba = 0;
        std::vector<FillRectInstance> instances;
        TileFillRectBatch(StyleKey sk, std::uint32_t c) : style_key(sk), rgba(c) {}
    };

    /// One tile's worth of triangulated filled-polygon verts sharing one @ref StyleKey.
    struct TileFillPolyBatch {
        StyleKey               style_key = 0;
        std::uint32_t          rgba = 0;
        std::vector<PosVertex> verts;
        TileFillPolyBatch(StyleKey sk, std::uint32_t c) : style_key(sk), rgba(c) {}
    };

    /// One tile's worth of thick (screen-width) lines sharing one @ref StyleKey.
    struct TileThickLineBatch {
        StyleKey                       style_key = 0;
        std::uint32_t                  rgba = 0;
        std::vector<ThickLineInstance> instances;
        TileThickLineBatch(StyleKey sk, std::uint32_t c) : style_key(sk), rgba(c) {}
    };

    /// One tile's worth of dashed lines sharing one @ref StyleKey.
    struct TileDashedLineBatch {
        StyleKey                        style_key = 0;
        std::uint32_t                   rgba = 0;
        std::vector<DashedLineInstance> instances;
        TileDashedLineBatch(StyleKey sk, std::uint32_t c) : style_key(sk), rgba(c) {}
    };

    /// All geometry recorded for a single screen-space tile, split into one
    /// vector of style batches per primitive type. The ctor reserves
    /// @ref kBatchInitialReserve per type to avoid reallocation churn on the
    /// recording hot path. @c world_bounds / @c tile_x / @c tile_y identify
    /// the tile for chunk assignment at @ref flush() time.
    struct RhiTileBatch {
        RhiTileBatch() {
            thin_line_batches.reserve(kBatchInitialReserve);
            fill_rect_batches.reserve(kBatchInitialReserve);
            fill_poly_batches.reserve(kBatchInitialReserve);
            thick_line_batches.reserve(kBatchInitialReserve);
            dashed_line_batches.reserve(kBatchInitialReserve);
        }

        rectangle                         world_bounds;  ///< World extent of this tile, copied into each emitted @ref Chunk.
        std::uint16_t                     tile_x = 0;    ///< Tile column in the @ref kTileGridDimension grid.
        std::uint16_t                     tile_y = 0;    ///< Tile row in the @ref kTileGridDimension grid.
        std::vector<TileThinLineBatch>    thin_line_batches;
        std::vector<TileFillRectBatch>    fill_rect_batches;
        std::vector<TileFillPolyBatch>    fill_poly_batches;
        std::vector<TileThickLineBatch>   thick_line_batches;
        std::vector<TileDashedLineBatch>  dashed_line_batches;

        /// True when this tile holds no geometry of any primitive type.
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

    /// Pack the current recording state (color, line width, dash) plus the
    /// given primitive type into a @ref StyleKey. The dash bits are only set
    /// for @ref PrimitiveType::DashedLine.
    StyleKey current_style_key(PrimitiveType primitive_type,
                               float         line_width_px = 0.0f) const;

    /// @name Find-or-create the per-@ref StyleKey batch within one tile.
    /// Returns the existing batch for @p style_key, or appends a fresh one.
    /// @{
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
    /// @}

    /// @name Append already-tile-clipped geometry into one tile's style batch.
    /// Lowest level: the caller has clipped the primitive to @p tile's world
    /// bounds; these just push the vertices/instance via @c ensure_*_batch.
    /// @{
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
    /// @}

    /// @name Bin one primitive across every tile it overlaps.
    /// Compute the primitive's tile span, then for each covered tile clip the
    /// geometry to the tile's world bounds and append it there. These are the
    /// per-primitive entry points used by @ref dispatch_commands_to_tiles.
    /// @{
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
    /// @}

    /// Append a thick-line segment into one tile's thick-line batch (clipped).
    void append_thick_line_segment(RhiTileBatch& tile,
                                   const point2d& start,
                                   const point2d& end,
                                   StyleKey      style_key,
                                   std::uint32_t rgba);
    /// Bin a thick line across the tiles it overlaps.
    void append_thick_line_to_tiles(const point2d& start,
                                    const point2d& end,
                                    StyleKey   style_key,
                                    std::uint32_t rgba);
    /// Bin one edge of a thick rectangle outline; reuses the thick-line pool
    /// and pipeline (forwards to @ref append_thick_line_to_tiles).
    void append_thick_draw_segment_to_tiles(const point2d& start,
                                            const point2d& end,
                                            StyleKey   style_key,
                                            std::uint32_t rgba);

    /// Append a dashed-line segment into one tile's dashed batch (clipped),
    /// carrying @p phase_world so the dash pattern stays continuous across
    /// tile boundaries.
    void append_dashed_segment(RhiTileBatch& tile,
                               const point2d& start,
                               const point2d& end,
                               float         phase_world,
                               StyleKey      style_key,
                               std::uint32_t rgba);
    /// Bin a dashed line across the tiles it overlaps, tracking cumulative
    /// world-space phase per sub-segment.
    void append_dashed_line_to_tiles(const point2d& start,
                                     const point2d& end,
                                     StyleKey   style_key,
                                     std::uint32_t rgba);
    /// Bin one edge of a dashed rectangle outline; reuses the dashed-line pool.
    void append_dashed_draw_segment_to_tiles(const point2d& start,
                                             const point2d& end,
                                             StyleKey   style_key,
                                             std::uint32_t rgba);

    /// (Re)create the overlay QImage at the current size/DPR, clear it to
    /// transparent, and begin its painter so overlay draw calls can paint.
    void begin_overlay_frame();
    /// Rebuild the overlay layer by replaying the cached deferred overlay
    /// commands (text/arcs/surfaces) without re-running the draw callback;
    /// used by the camera-only redraw path.
    void render_cached_overlay();
    /// Allocate/resize the tile grid and recompute each tile's world bounds
    /// for the current visible world.
    void ensure_tile_grid();
    /// Clear per-tile geometry (the style batches) while keeping the grid.
    void clear_tile_geometry();
    /// Clear the recorded per-band command queues (except @c m_cmd_arrows,
    /// which @ref build_scene_buffers consumes later).
    void clear_commands();
    /// Parallel worker: replay the recorded commands for one horizontal
    /// @p band of tile rows into their tiles. Bands partition the tile rows so
    /// workers write disjoint tiles without locking.
    void dispatch_commands_to_tiles(int band);
    /// Band index owning tile row @p ty.
    int  band_for_tile_row(int ty) const { return std::min(ty / m_rows_per_band, m_n_bands - 1); }
    /// First tile row in @p band.
    int  band_ty_min(int band) const { return band * m_rows_per_band; }
    /// Last tile row in @p band (clamped to the grid).
    int  band_ty_max(int band) const { return std::min((band + 1) * m_rows_per_band - 1, kTileGridDimension - 1); }
    /// Clamp a world X to a valid tile column index.
    int clamp_tile_x(double x) const;
    /// Clamp a world Y to a valid tile row index.
    int clamp_tile_y(double y) const;
    /// Flatten a (column, row) tile coordinate to a linear grid index.
    int tile_index(int tile_x, int tile_y) const;
    /// Tile batch at (column, row).
    RhiTileBatch& tile_at(int tile_x, int tile_y);
    /// Concatenate the per-tile style batches into @ref SceneBuffers, emitting
    /// one @ref Chunk per tile per style, then append the un-tiled arrows.
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

    /// Recorded 1-pixel line: @c sk is the packed @ref StyleKey (rgba in its
    /// lower 32 bits), @c (x0,y0)–(x1,y1) the world-space endpoints.
    struct ThinLineCmd   { StyleKey sk; float x0, y0, x1, y1; };
    /// Recorded filled rectangle spanning the world-space opposite corners
    /// @c (x0,y0) and @c (x1,y1); @c sk is the packed @ref StyleKey.
    struct FillRectCmd   { StyleKey sk; float x0, y0, x1, y1; };
    /// Recorded filled triangle with world-space vertices @c (x0,y0),
    /// @c (x1,y1), @c (x2,y2); @c sk is the packed @ref StyleKey.
    struct FillTriCmd    { StyleKey sk; float x0, y0, x1, y1, x2, y2; };
    /// Recorded stroked line whose world-space width is carried in @c sk;
    /// @c (x0,y0)–(x1,y1) are the world-space endpoints.
    struct ThickLineCmd  { StyleKey sk; float x0, y0, x1, y1; };
    /// Recorded dashed line; @c sk encodes both width and dash pattern, with
    /// @c (x0,y0)–(x1,y1) the world-space endpoints.
    struct DashedLineCmd { StyleKey sk; float x0, y0, x1, y1; };
    /// Recorded arrow: @c (ax,ay) is the world-space anchor and @c (dx,dy) the
    /// direction the head points; the head is expanded to a fixed pixel size in
    /// the vertex shader, so this command is never tile-binned (see
    /// @c m_cmd_arrows). @c sk is the packed @ref StyleKey.
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
