#pragma once

#include "ezgl/irenderer.hpp"

#include <QImage>

/**
 * @file render_backend.hpp
 *
 * @brief Abstract base for ezgl rendering backends + shared backend enums.
 *
 * Three concrete backends implement @ref ezgl::render_backend; ezgl::canvas
 * picks one at runtime based on @ref ezgl::renderer_type. See
 * `doc/renderers.md` for the user-facing comparison.
 *
 * | Backend | Class | Header |
 * |---|---|---|
 * | `immediate` | @c ezgl::immediate_backend | `immediate_backend.hpp` |
 * | `deferred`  | @c ezgl::deferred_backend  | `deferred_backend.hpp` |
 * | `rhi`       | @c ezgl::rhi_backend       | `rhi_backend.hpp` |
 *
 * The rhi backend is the default and is composed of four cooperating
 * classes: @c rhi_backend (lifecycle), @c rhi_renderer (recording +
 * @c irenderer impl), @c RhiCanvasWidget (Qt widget + thread inbox),
 * @c RhiSceneRenderer (GPU resources). See `rhi_renderer.hpp` for the
 * component map.
 */

namespace ezgl {

/**
 * @brief Reasons corresponding to a change in the view.
 */
enum class view_change_reason {
    pan,          ///< The visible world moved without changing zoom level.
    zoom_in,      ///< The visible world area decreased.
    zoom_out,     ///< The visible world area increased.
    pan_zoom_in,  ///< The visible world moved and world area decreased.
    pan_zoom_out, ///< The visible world moved and world area increased.
};

/// Application-supplied scene-drawing callback.
using draw_canvas_fn = void (*)(renderer*);

/**
 * @brief Ask the client to decide whether cached scene geometry can be reused for the new view.
 * 
 * Note: the world dimension has been updated by the time this callback is invoked.
 * 
 * @param reason Reason that triggers this view change (e.g. pan, zoom_in).
 * @param g Renderer used to retreive information about the updated world.
 * 
 * @return Return true to update only the camera transform/overlay, or false to force a full redraw.
 */
using decide_reuse_geometry_fn = std::function<bool(view_change_reason reason, renderer* g)>;

/// Backend identifier used by @c canvas::set_renderer_type to select
/// which @ref render_backend subclass to instantiate.
enum class renderer_type {
  /// Immediate-mode QPainter: every draw call executed synchronously
  /// (@ref immediate_backend).
  immediate,
  /// QPainter with the deferred_renderer path: geometry recorded each frame and
  /// replayed in batches grouped by pen/brush style, minimising QPainter state
  /// changes vs the immediate path (@ref deferred_backend).
  deferred,
  /// Qt RHI GPU-accelerated backend; the default (@ref rhi_backend).
  rhi
};

/// MSAA sample count for the rhi backend (both on-screen QRhiWidget and the
/// offscreen render_to_image path use it; every QRhiGraphicsPipeline must
/// match). Valid Qt values are 1, 2, 4, 8, 16; 1 disables MSAA.
///
/// We default to 1 (MSAA off). With sample counts > 1, the multisample
/// coverage resolve thickens 1-pixel-wide primitives: each pixel a thin
/// diagonal line touches gets partial coverage from multiple subsamples
/// and is blended toward the line color, so the line reads as ~2 px wide
/// (and softer) instead of crisp 1 px. For scenes dominated by 1 px
/// lines, that visible widening is worse than the aliasing MSAA was
/// meant to fix.
inline constexpr int EZGL_RHI_SAMPLE_COUNT = 1;

/// Stable short name for a @ref renderer_type, suitable for log lines and
/// test matrices.
inline constexpr const char* renderer_type_name(renderer_type t) noexcept
{
    switch (t) {
        case renderer_type::immediate: return "immediate";
        case renderer_type::deferred:  return "deferred";
        case renderer_type::rhi:       return "rhi";
        default:                       return "immediate";
    }
}

/**
 * @brief Abstract rendering backend owned by canvas.
 *
 * Each concrete backend encapsulates one rendering path's full lifecycle:
 * frame scheduling, resize handling, and per-frame draw dispatch. canvas
 * selects the right implementation at @c set_renderer_type() time and
 * routes all redraw / resize / capture requests through this interface.
 *
 * Lifecycle pattern used by callers (optional; RHI only — others no-op):
 * @code
 *   backend->suspend_redraw();   // hold show/resize-driven renders
 *     ...show or resize the window; run initial setup...
 *   backend->resume_redraw();    // flush the single pending render once
 * @endcode
 * A direct @ref redraw() / @ref redraw_camera_only() is NOT held while
 * redrawing is suspended — those dispatch immediately; only @ref on_resize defers.
 * See @ref suspend_redraw for why this bracket exists and why only RHI honours
 * it, and @ref rhi_backend for the pending-flag handling behind it.
 */
class render_backend {
public:
    virtual ~render_backend() = default;

    /// Full redraw: re-run the application draw callback to rebuild the
    /// scene from scratch. Use when scene state (block colors, route
    /// trees, …) has changed.
    virtual void redraw() = 0;

    /**
     * @brief A camera-only (MVP) redraw when cached geometry is reusable, otherwise a full redraw. Meaningful on the RHI path only.
     *
     * @param reason Reason that triggers this view change (e.g. pan, zoom_in).
     * 
     * On non-RHI paths, this always falls back to a full redraw.
     */
    virtual void redraw_at_view_change(view_change_reason reason) = 0;

    /// Suspend redrawing around scene setup (optional; honoured only by RHI).
    ///
    /// suspend_redraw()/resume_redraw() bracket "show the window + run the user's
    /// initial setup" so the RHI path produces exactly one correct GPU frame at
    /// the end (@ref resume_redraw), instead of several premature or redundant
    /// ones during window construction. (On RHI, resize events fired while the
    /// window is shown are recorded as pending and flushed once at
    /// @ref resume_redraw; see @ref rhi_backend.)
    ///
    /// Why only RHI: the QPainter backends (immediate/deferred) treat these as
    /// no-ops, because @c QWidget::update() already defers to a single coalesced
    /// paintEvent and their CPU paint is cheap and idempotent — Qt gives them
    /// the same coalescing for free. RHI flushes straight to the GPU, so it
    /// relies on the explicit suspend_redraw()/resume_redraw() bracket instead.
    ///
    /// Between suspend_redraw() and resume_redraw(), ezgl does its setup:
    /// shows the window, wires up the button callbacks, and runs
    /// @c initial_setup.
    virtual void suspend_redraw() {}
    /// @see suspend_redraw
    virtual void resume_redraw() {}

    /// Resize notification, called from the canvas widget's @c resizeEvent()
    /// — @ref DrawingAreaWidget for the immediate/deferred backends,
    /// @ref RhiCanvasWidget for RHI. Qt calls @c resizeEvent automatically
    /// whenever the widget's geometry changes (dragging the window corner,
    /// maximise, a layout change); the same signal is also emitted on
    /// @c showEvent.
    ///
    /// The widget emits @c resized, and canvas responds by first updating the
    /// camera's widget size and then calling this. So it *is* camera-related —
    /// but the trigger is the drawing surface changing size, not the zoom
    /// level. Anything sized to the window (render targets, swap chains, the
    /// overlay image) therefore has to be recreated. For RHI the GPU geometry
    /// is still valid, so this usually takes the camera-only path (just a new
    /// MVP) rather than a full redraw.
    ///
    /// (A *swap chain* is the small set of images the GPU rotates through: it
    /// draws into one while the screen displays another, then they swap. Only
    /// the GPU backend has one; see @ref rhi_types.hpp's glossary.)
    ///
    /// @param w New width of the drawing canvas widget, in logical pixels.
    /// @param h New height of the drawing canvas widget, in logical pixels.
    ///
    /// These are the canvas widget's own size, not the top-level window's:
    /// they come straight from the widget's @c width() / @c height() as it
    /// emits @c resized. They are logical (device-independent) pixels, so a
    /// backend that needs device pixels must scale by the widget's device
    /// pixel ratio itself.
    virtual void on_resize(int w, int h) = 0;

    /// Return a transient @c renderer instance suitable for one-off
    /// animation overlays (e.g. mouse hit-test highlights painted on top
    /// of the cached scene without rebuilding it). The returned pointer
    /// is owned by the backend and lives until the next frame.
    virtual renderer* create_animation_renderer() = 0;

    /**
     * Render a frame and return it as a QImage. Used by
     * @c canvas::render_to_image() to back @c save_graphics(), PDF/PNG export,
     * and headless visual regression tests.
     *
     * Every backend must implement this: the QPainter backends paint into an
     * off-screen QImage, while @ref rhi_backend does a GPU readback.
     *
     * @param w  Output width in pixels.
     * @param h  Output height in pixels.
     */
    virtual QImage render_to_image(int w, int h) = 0;
};

} // namespace ezgl
