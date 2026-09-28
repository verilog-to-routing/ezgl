#pragma once

#include "ezgl/irenderer.hpp"
#include "ezgl/qt/painter.hpp"

#include <QLineF>
#include <QPolygonF>
#include <QRectF>
#include <QFont>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

namespace ezgl {

// ---- style keys ----------------------------------------------------------

/**
 * Style identity for stroked primitives (lines, rect outlines). Two primitives
 * sharing a LineStyleKey can be drawn with the same QPen, so they are batched
 * together to minimise QPainter state changes during flush. See @ref
 * deferred_renderer for what "batched" means in practice: primitives with
 * the same key are grouped so the whole group can be drawn with one
 * QPen/QBrush setup instead of one per primitive.
 */
struct LineStyleKey {
    uint32_t color_rgba;   ///< Packed color: r | g<<8 | b<<16 | a<<24.
    uint16_t line_width;   ///< current_line_width clamped to uint16.
    uint8_t  line_cap;     ///< line_cap enum cast to uint8.
    uint8_t  line_dash;    ///< line_dash enum cast to uint8.

    /// @return The four fields packed into one value for use as a batch-map key.
    uint64_t key() const {
        return uint64_t(color_rgba)
             | (uint64_t(line_width) << 32)
             | (uint64_t(line_cap)   << 48)
             | (uint64_t(line_dash)  << 56);
    }
};

/**
 * Style identity for filled primitives (rectangles, polygons). Fills have no
 * stroke attributes, so only color distinguishes them for batching (see
 * @ref deferred_renderer for what batching means in practice).
 */
struct FillStyleKey {
    uint32_t color_rgba;   ///< Packed color: r | g<<8 | b<<16 | a<<24.

    /// @return The color, used directly as a batch-map key.
    uint64_t key() const { return color_rgba; }
};

// ---- batch storage -------------------------------------------------------
//
// Each batch groups primitives that share one style key, so flush() can set
// the QPen/QBrush once and emit the whole vector in a single QPainter pass.

/// All lines sharing one LineStyleKey, drawn with a single QPen.
struct LineBatch {
    LineStyleKey        style;   ///< Shared stroke style for every line in the batch.
    std::vector<QLineF> lines;   ///< Lines collected under this style.
};

/// All filled rectangles sharing one FillStyleKey, drawn with a single QBrush.
struct FillRectBatch {
    FillStyleKey        style;   ///< Shared fill style for every rectangle in the batch.
    std::vector<QRectF> rects;   ///< Filled rectangles collected under this style.
};

/// All rectangle outlines sharing one LineStyleKey, drawn with a single QPen.
struct DrawRectBatch {
    LineStyleKey        style;   ///< Shared stroke style for every outline in the batch.
    std::vector<QRectF> rects;   ///< Outlined rectangles collected under this style.
};

/// All filled polygons sharing one FillStyleKey, drawn with a single QBrush.
struct FillPolyBatch {
    FillStyleKey            style;   ///< Shared fill style for every polygon in the batch.
    std::vector<QPolygonF>  polys;   ///< Filled polygons collected under this style.
};

/**
 * Snapshot of the renderer's drawing state taken when a command is recorded.
 *
 * Primitives with a simple style (lines, rects, polys) batch by style key, but
 * richer commands (arcs, text, surfaces) carry a full state snapshot so flush()
 * can restore the exact pen/brush/font/justification/rotation each was issued
 * with — replaying them faithfully regardless of recording order.
 */
struct DeferredPainterState {
    t_coordinate_system coordinate_system = WORLD;            ///< WORLD or SCREEN at record time.
    color               draw_color {0, 0, 0, 255};           ///< Stroke/fill color.
    int                 line_width = 0;                      ///< Line width in pixels (0 means 1).
    line_cap            line_cap_style = line_cap::butt;      ///< Line-cap style.
    line_dash           line_dash_style = line_dash::none;    ///< Line-dash pattern.
    double              rotation_radians = 0.0;              ///< Text rotation, in radians.
    justification       horiz_just = justification::center;   ///< Horizontal text/surface anchoring.
    justification       vert_just = justification::center;    ///< Vertical text/surface anchoring.
    QFont               font;                                ///< Font for text commands.
};

/// A deferred (elliptic) arc, stroked or filled, replayed during flush.
struct DeferredArcCommand {
    DeferredPainterState state;             ///< Painter state captured at record time.
    point2d              center;            ///< Arc center.
    double               radius_x = 0.0;    ///< Horizontal radius.
    double               radius_y = 0.0;    ///< Vertical radius (== radius_x for a circle).
    double               start_angle = 0.0; ///< Start angle, degrees CCW from +x.
    double               extent_angle = 0.0;///< Angular sweep, degrees.
    bool                 fill = false;      ///< True fills the wedge, false strokes the arc.
};

/// A deferred text string, replayed during flush.
struct DeferredTextCommand {
    DeferredPainterState state;        ///< Painter state captured at record time.
    point2d              point;        ///< Anchor position (interpreted per state's coordinate system).
    std::string          text;         ///< The string to draw.
    double               bound_x = 0.0; ///< Max width; text is skipped if it exceeds this (DBL_MAX = unbounded).
    double               bound_y = 0.0; ///< Max height; text is skipped if it exceeds this (DBL_MAX = unbounded).
    bool                 scale_font_with_camera = false; ///< WORLD text: shrink the font as the view zooms out.
    double               recorded_world_scale = 1.0;     ///< Camera world scale at record time, for the rescale ratio.
    point2d              screen_offset_px = {0.0, 0.0};  ///< One-shot screen-pixel offset applied at replay.
};

/// A deferred image-surface blit, replayed during flush.
struct DeferredSurfaceCommand {
    DeferredPainterState state;             ///< Painter state captured at record time.
    surface*             p_surface = nullptr;///< Image to draw (not owned).
    point2d              anchor_point;       ///< Anchor position, placed per the state's justification.
    double               scale_factor = 1.0; ///< Uniform scale applied to the surface.
};

/**
 * A small filled triangle (e.g., an arrow head) that follows a world
 * position but stays at a constant SCREEN size at every zoom level.
 *
 * anchor_world is the centroid of the triangle in WORLD coords; it
 * pans/zooms with the camera. The three corner offsets are stored in
 * SCREEN PIXELS measured at record time and applied at replay time as
 * pixel-space offsets from anchor_world's projected screen position —
 * so the triangle's pixel size stays fixed regardless of zoom.
 */
struct DeferredArrowTriangleCommand {
    DeferredPainterState state;
    point2d              anchor_world;
    point2d              offset_a_px;
    point2d              offset_b_px;
    point2d              offset_c_px;
};

/// One recorded deferred command — any of the deferred command types. Stored in
/// submission order in m_commands and dispatched with std::visit during
/// replay: std::visit looks at which of the four command types a given
/// DeferredCommand currently holds and calls the matching draw code for it,
/// so replay doesn't need an if/else chain to figure out each command's type.
/// (These commands can't be batched by style like lines/rects, so each is
/// replayed individually.)
using DeferredCommand =
    std::variant<DeferredArcCommand,
                 DeferredTextCommand,
                 DeferredSurfaceCommand,
                 DeferredArrowTriangleCommand>;

// ---- deferred_renderer ---------------------------------------------------

/**
 * QPainter renderer that *records* a frame's draw calls and replays them later,
 * instead of painting immediately. This buys two things over the immediate
 * renderer: draw calls are reordered to minimise QPainter pen/brush changes,
 * and the recorded frame can be re-drawn after the camera's view changes
 * (the user pans or zooms) without re-running the application's draw
 * callback — see @ref replay for how that works.
 *
 * Two very different callers use it:
 * - @ref deferred_backend renders an *entire* frame through it — every
 *   primitive the application draws.
 * - @ref rhi_renderer uses it only *partially*, as an **overlay layer**: the
 *   bulk WORLD geometry is drawn on the GPU, and only the primitives the GPU
 *   path doesn't handle — text, arcs, surfaces, and SCREEN-coordinate
 *   primitives — are forwarded here, painted into an off-screen QImage, and
 *   composited on top of the GPU layers. So when this doc says "rhi", read it
 *   as "rhi's overlay subset", not the whole scene.
 *
 * ## Two storage paths
 *
 * Each draw call lands in one of two stores, chosen by how cheaply it can be
 * described:
 *
 * 1. **Style-batched primitives** — @c draw_line, @c draw_rectangle,
 *    @c fill_rectangle, @c fill_poly. Their geometry is projected to screen
 *    space immediately and appended to a batch keyed by a @ref LineStyleKey /
 *    @ref FillStyleKey (color + stroke attributes). All primitives sharing a
 *    style land in the same batch, so replay sets the QPen/QBrush once and
 *    emits the whole batch in a single QPainter call. This is the hot path for
 *    bulk geometry (nets, routes, channels).
 *
 * 2. **Deferred commands** — @c draw_text, the arc calls, @c draw_surface,
 *    @c fill_arrow_pointer_triangle. These carry per-instance state that a
 *    style key can't capture (font, rotation, justification) and/or must be
 *    re-projected per camera (text that rescales with zoom, arrows kept at a
 *    constant pixel size). Each is recorded as a @ref DeferredCommand
 *    with a full @ref DeferredPainterState snapshot and is kept in *world*
 *    coordinates. World-space commands are also inserted into a coarse
 *    spatial grid (@ref index_world_command) so that replay can find
 *    the ones overlapping the view without scanning every command.
 *    SCREEN-space commands have no world position to index, so they go into a
 *    plain vector (@ref m_unindexed_commands) instead. The grid can't narrow
 *    them down, so each is checked individually every frame by the usual
 *    visibility test — cheap, as there are few of them.
 *
 * ## When things happen
 *
 * - **Record time** (inside the app's draw callback): the calls above only
 *   *store* geometry/commands; nothing is painted yet, and no visibility test
 *   is done.
 * - **Replay time** (@ref replay): visibility culling happens *here*, against
 *   the *current* camera — not at record time — which is what lets the same
 *   recording be replayed at a different zoom/pan. @ref replay() then:
 *     1. culls each batch to the primitives currently on screen;
 *     2. culls commands via the spatial index, then a per-command
 *        visibility test against the current view;
 *     3. draws the visible batches (one QPen/QBrush set-up per batch);
 *     4. draws the visible commands, applying each command's captured
 *        state and any camera-dependent transform (text rescale, arrow sizing).
 * - @ref flush() = @ref replay() then @ref reset(). @ref deferred_backend
 *   re-records every frame and calls @ref flush(). @ref rhi_renderer (only its
 *   overlay subset — text/arcs/surfaces/screen-coordinate primitives, see
 *   above) calls @ref replay() repeatedly across camera-only updates so those
 *   overlays follow the pan/zoom without re-recording, and @ref reset() only
 *   when the scene actually changes.
 */
class deferred_renderer : public irenderer {
public:
    /**
     * Construct a deferred renderer over the given painter and camera. The
     * record buffers, batches, and command spatial index all start empty; the
     * command index grid is sized lazily on first use (see
     * @ref ensure_command_index_grid).
     *
     * @param painter Backend painter used to issue draw primitives.
     * @param cam     Camera supplying the current view, scale, and
     *                world→screen transform.
     */
    deferred_renderer(Painter *painter,
                      camera *cam);

    ~deferred_renderer() override = default;

    // ---- irenderer: hot-path draw calls (batched) --------------------------
    // These record into the per-state vertex batches (see DeferredPainterState)
    // rather than painting immediately; the geometry is emitted at @ref replay()
    // time. Semantics otherwise match the irenderer base declarations.

    /// @name Record a line segment into the current state's line batch.
    /// @{
    void draw_line(const point2d& start, const point2d& end) override;
    /// @}

    /// @name Record a filled rectangle into the current state's fill batch.
    /// @{
    void fill_rectangle(const point2d& start, const point2d& end) override;
    void fill_rectangle(const point2d& start, double width, double height) override;
    void fill_rectangle(const rectangle& r) override;
    /// @}

    /// @name Record a rectangle outline into the current state's line batch.
    /// @{
    void draw_rectangle(const point2d& start, const point2d& end) override;
    void draw_rectangle(const point2d& start, double width, double height) override;
    void draw_rectangle(const rectangle& r) override;
    /// @}

    // ---- irenderer: draw calls (deferred to command queue) -----------------
    // These append a DeferredCommand (with a captured painter state) to
    // the command queue instead of painting immediately; world-space commands
    // are also entered into the spatial index for replay-time culling.
    // Semantics otherwise match the irenderer base declarations.

    /// @name Record a filled polygon as a deferred command.
    /// @{
    void fill_poly(const std::vector<point2d>& points) override;
    /// @}
    /// @name Record a filled triangle as a deferred command.
    /// @{
    void fill_triangle(const point2d& a, const point2d& b, const point2d& c) override;
    /// @}
    /// @name Record a screen-sized arrow-pointer triangle as a deferred command,
    /// capturing the per-vertex pixel offsets so its size stays zoom-invariant.
    /// @{
    void fill_arrow_pointer_triangle(const point2d& anchor_world,
                                      const point2d& dir_world,
                                      float          arrow_size_px) override;
    /// @}
    /// @name Record an elliptic-arc outline as a deferred command.
    /// @{
    void draw_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    /// @}
    /// @name Record a circular-arc outline as a deferred command.
    /// @{
    void draw_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;
    /// @}
    /// @name Record a filled elliptic arc as a deferred command.
    /// @{
    void fill_elliptic_arc(const point2d& center, double radius_x, double radius_y,
                           double start_angle, double extent_angle) override;
    /// @}
    /// @name Record a filled circular arc as a deferred command.
    /// @{
    void fill_arc(const point2d& center, double radius,
                  double start_angle, double extent_angle) override;
    /// @}
    /// @name Record a text label as a deferred command.
    /// @{
    void draw_text(const point2d& point, std::string const& text) override;
    void draw_text(const point2d& point, std::string const& text,
                   double bound_x, double bound_y) override;
    /// @}
    /// @name Record a surface (image) blit as a deferred command.
    /// @{
    void draw_surface(surface* p_surface, const point2d& anchor_point,
                      double scale_factor = 1) override;
    /// @}

    // ---- Replay / flush ----------------------------------------------------

    // Replay all recorded batches and commands to the QPainter,
    // keeping them so they can be replayed again (e.g. rhi replaying its
    // overlay subset on a camera-only redraw — see the class doc).
    void replay();

    // Discard all recorded batches and commands.
    void reset();

    // Replay, then reset.
    void flush();

private:
    /// Lazily size the command spatial-index grid to the camera's initial world
    /// (degenerate dimensions are clamped to epsilon); a no-op once built.
    void ensure_command_index_grid();
    /// @name Map a world coordinate to a grid column / row, clamped to the index bounds.
    /// @{
    int clamp_command_tile_x(double x) const;
    int clamp_command_tile_y(double y) const;
    /// @}
    /// Insert a world-space command into every index bucket its
    /// @p bounds overlaps, so a later visible-world query can find it.
    void index_world_command(std::uint32_t command_index,
                                     rectangle      bounds);
    /// Snapshot the current painter/renderer state for replay of a deferred command.
    DeferredPainterState capture_painter_state() const;
    /// Restore a previously captured painter/renderer state onto the painter.
    void apply_painter_state(const DeferredPainterState& state);

    /// The current viewport rectangle in screen (widget) pixels.
    QRectF screen_viewport_rect() const;
    /// @name Test a primitive against the screen viewport, used to cull SCREEN-coordinate draws.
    /// @{
    bool screen_rect_visible(const QRectF& rect, double padding = 0.0) const;
    bool screen_line_visible(const QLineF& line, double line_width) const;
    bool screen_arc_visible(const point2d& center,
                            double radius_x,
                            double radius_y) const;
    bool screen_text_visible(const point2d& point,
                             const std::string& text,
                             double bound_x,
                             double bound_y) const;
    bool screen_surface_visible(surface *p_surface,
                                const point2d& point,
                                double scale_factor) const;
    /// @}

    // ---- replay() stages ---------------------------------------------------
    // A batch culled to only its currently-visible primitives, ready to draw.
    //
    // This holds a compacted copy of the visible primitives rather than, say,
    // a "visible" bool on each primitive in the base batch: draw_visible_batches
    // hands each batch straight to a single QPainter::drawLines/drawRects call,
    // which needs a contiguous array of exactly the primitives to draw. A bool
    // flag wouldn't avoid that compaction step (QPainter can't skip flagged
    // entries mid-array), so it would only add state to reset every frame
    // without removing the copy.
    struct VisibleLineBatch { LineStyleKey style;        std::vector<QLineF>    lines; };
    struct VisibleRectBatch { FillStyleKey fill_style;
                              LineStyleKey line_style;   std::vector<QRectF>    rects; };
    struct VisiblePolyBatch { FillStyleKey style;        std::vector<QPolygonF> polys; };
    // All four batch kinds after visibility culling (see cull_visible_batches).
    struct VisibleBatches {
        std::vector<VisibleLineBatch> lines;
        std::vector<VisibleRectBatch> fill_rects;
        std::vector<VisibleRectBatch> draw_rects;
        std::vector<VisiblePolyBatch> fill_polys;
    };

    // Per-frame scratch for replay(), held as a member (see m_replay_cache) so
    // the buffers reuse their heap storage across frames — each replay() clears
    // and refills them instead of reallocating.
    struct ReplayCache {
        VisibleBatches                             visible_batches;    ///< Culled batches to draw.
        std::vector<std::uint32_t>                 candidate_commands; ///< Command indices possibly in view.
        std::vector<const DeferredCommand*> visible_commands;    ///< Commands that passed the cull.

        // Empty every buffer for a fresh frame, keeping their heap capacity.
        void clear() {
            visible_batches.lines.clear();
            visible_batches.fill_rects.clear();
            visible_batches.draw_rects.clear();
            visible_batches.fill_polys.clear();
            candidate_commands.clear();
            visible_commands.clear();
        }
    };

    // The stage functions below fill caller-provided output buffers (rather
    // than returning by value) so replay() can reuse persistent scratch
    // members across frames — each call clears its @p out and refills it,
    // keeping the heap storage instead of reallocating it every frame.

    // Stage 1a: cull each recorded batch to the primitives currently on screen.
    void cull_visible_batches(VisibleBatches& out) const;
    // Stage 1b: collect command indices that might be in view (the
    // always-replayed unindexed ones plus a spatial-index query of the visible
    // world), sorted into record order.
    void gather_candidate_commands(std::vector<std::uint32_t>& out);
    // Stage 1c: from the candidates, keep the commands that pass a per-command
    // visibility test against the current camera (applies painter state while
    // measuring text/surface extents).
    void select_visible_commands(const std::vector<std::uint32_t>& candidates,
                                         std::vector<const DeferredCommand*>& out);
    // Stage 2a: paint the culled batches (one QPen/QBrush set-up per batch).
    void draw_visible_batches(const VisibleBatches& batches);
    // Stage 2b: paint the visible commands on top, applying each
    // command's captured state and any camera-dependent transform.
    void draw_visible_commands(
        const std::vector<const DeferredCommand*>& commands);

    // Per-command visibility helpers (world coordinates). The text one
    // also resolves the camera-rescaled font into @p state for replay.
    bool resolve_text_replay_state(const DeferredTextCommand& cmd, DeferredPainterState& state);
    bool world_arc_visible(const point2d& center, double radius_x, double radius_y);
    bool world_text_visible(const point2d& point, const std::string& text,
                            double bound_x, double bound_y);
    bool world_surface_visible(surface* p_surface, const point2d& point, double scale_factor);

    /// @name Build the batch lookup key for the current line / fill state.
    /// @{
    LineStyleKey current_line_style() const;
    FillStyleKey current_fill_style() const;
    /// @}

    /// @name Append a primitive to the batch for the given style key, creating the batch on first use.
    /// @{
    void add_line(const LineStyleKey &s, QLineF line);
    void add_fill_rect(const FillStyleKey &s, QRectF rect);
    void add_draw_rect(const LineStyleKey &s, QRectF rect);
    void add_fill_poly(const FillStyleKey &s, QPolygonF poly);
    /// @}

    /// Convert a world-space rectangle (two opposite corners) to screen pixels.
    QRectF to_screen_rect(const point2d& start, const point2d& end);

    /// Record an arc as a deferred command; @p fill selects filled vs. outline.
    void push_arc_command(const point2d& center, double radius_x, double radius_y,
                          double start_angle, double extent_angle, bool fill);

    // Batch vectors — maintain submission order for painter's algorithm.
    std::vector<LineBatch>     m_line_batches;     ///< Line batches in submission order.
    std::vector<FillRectBatch> m_fill_rect_batches; ///< Filled-rectangle batches in submission order.
    std::vector<DrawRectBatch> m_draw_rect_batches; ///< Rectangle-outline batches in submission order.
    std::vector<FillPolyBatch> m_fill_poly_batches; ///< Filled-polygon batches in submission order.

    // Fast lookup: style key → index into the vectors above.
    std::unordered_map<uint64_t, size_t> m_line_idx;      ///< LineStyleKey → index into m_line_batches.
    std::unordered_map<uint64_t, size_t> m_fill_rect_idx; ///< FillStyleKey → index into m_fill_rect_batches.
    std::unordered_map<uint64_t, size_t> m_draw_rect_idx; ///< LineStyleKey → index into m_draw_rect_batches.
    std::unordered_map<uint64_t, size_t> m_fill_poly_idx; ///< FillStyleKey → index into m_fill_poly_batches.

    /// Recorded deferred commands (arcs/text/surfaces/arrow-triangles) in
    /// submission order; an index into this vector identifies a command in the
    /// spatial index and query buffers below.
    std::vector<DeferredCommand>  m_commands;

    // ---- command spatial index (world-space commands only) -----------------
    rectangle m_command_index_scene_bounds;       ///< World rectangle the index grid spans.
    double    m_command_index_tile_width = 1.0;   ///< Grid tile width in world units.
    double    m_command_index_tile_height = 1.0;  ///< Grid tile height in world units.
    /// Per-tile buckets of world-command indices (row-major over the grid); a
    /// command is listed in every tile its bounds overlap.
    std::vector<std::vector<std::uint32_t>> m_indexed_world_command_buckets;
    /// Indices of SCREEN-space commands, which aren't indexed by world position
    /// and are therefore always treated as candidates during a query.
    std::vector<std::uint32_t>           m_unindexed_commands;

    // A query = one search of the grid above for the commands in view, run once
    // per frame by gather_candidate_commands.
    //
    // A command wider than one tile is listed in every tile it touches, so the
    // search finds it once per tile and would collect it more than once. Say
    // command #7 is a long text label straddling two tiles:
    //
    //     bucket[tile 3,4] = [7, 9]
    //     bucket[tile 4,4] = [7, 12]
    //
    // With both tiles in view, #7 turns up twice and would be drawn twice.
    // The marks below skip that repeat: each command records the number of the
    // query that last collected it, and it is a repeat only if that number
    // equals the current query's. Above, #7 is marked when tile (3,4) collects
    // it, so tile (4,4) sees the mark and skips it.
    //
    // Numbering the queries (rather than a "seen" flag per command) means the
    // next query just increments the number: every old mark stops matching, so
    // nothing has to be cleared.

    /// Number of the query that last collected each command; 0 = never collected.
    /// Index-parallel to m_commands.
    std::vector<std::uint32_t>           m_command_query_marks;
    /// Number of the current query. It is 32-bit, so it eventually overflows
    /// back to 0 — and since 0 also means "never collected", untouched commands
    /// would then look like repeats and be dropped from the frame. On overflow
    /// gather_candidate_commands zeroes the marks and restarts the count at 1.
    std::uint32_t                        m_command_query_generation = 1;

    // Persistent scratch reused by replay() across frames (see ReplayCache).
    ReplayCache m_replay_cache;
};

} // namespace ezgl
