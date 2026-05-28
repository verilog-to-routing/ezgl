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

using draw_canvas_fn = void (*)(renderer*);

/// Backend identifier used by @c canvas::set_renderer_type to select
/// which @ref render_backend subclass to instantiate.
enum class renderer_type { immediate, deferred, rhi };

/// MSAA sample count for the rhi backend (both on-screen QRhiWidget and the
/// offscreen render_to_image path use it; every QRhiGraphicsPipeline must
/// match). Valid Qt values are 1, 2, 4, 8, 16; 1 disables MSAA.
///
/// We default to 1 (MSAA off). With sample counts > 1, the multisample
/// coverage resolve thickens 1-pixel-wide primitives: each pixel a thin
/// diagonal line touches gets partial coverage from multiple subsamples
/// and is blended toward the line color, so the line reads as ~2 px wide
/// (and softer) instead of crisp 1 px. For VPR's dense net / route /
/// channel rendering — which is dominated by 1 px strokes — that
/// visible widening is worse than the aliasing MSAA was meant to fix.
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
 * Lifecycle pattern used by callers:
 * @code
 *   backend->begin_deferred_redraw_cycle();  // optional: batch
 *     ...mutate state...
 *     backend->redraw();                     // or redraw_camera_only()
 *   backend->end_deferred_redraw_cycle();    // flushes pending
 * @endcode
 */
class render_backend {
public:
    virtual ~render_backend() = default;

    /// Full redraw: re-run the application draw callback to rebuild the
    /// scene from scratch. Use when scene state (block colors, route
    /// trees, …) has changed.
    virtual void redraw() = 0;

    /// Camera-only redraw: scene geometry is unchanged, only the camera
    /// transform / overlay layout differs. Backends that maintain a
    /// scene cache (rhi) can skip the user draw callback and just
    /// re-render with the new MVP. The immediate and deferred backends
    /// fall through to a full @ref redraw because they have no cache.
    virtual void redraw_camera_only() = 0;

    /// Optional batching window. Multiple @ref redraw / @ref
    /// redraw_camera_only calls between @c begin_ / @c end_ may coalesce
    /// into a single GPU frame. Default impl is a no-op for backends
    /// that don't benefit from batching.
    virtual void begin_deferred_redraw_cycle() {}
    /// @see begin_deferred_redraw_cycle
    virtual void end_deferred_redraw_cycle() {}

    /// Resize notification. Backends recreate render targets / swap chains
    /// here as needed.
    virtual void on_resize(int w, int h) = 0;

    /// Return a transient @c renderer instance suitable for one-off
    /// animation overlays (e.g. mouse hit-test highlights painted on top
    /// of the cached scene without rebuilding it). The returned pointer
    /// is owned by the backend and lives until the next frame.
    virtual renderer* create_animation_renderer() = 0;

    /**
     * Render a frame and return it as a QImage. Used by
     * @c canvas::render_to_image() to back @c save_graphics() and
     * headless visual regression tests.
     *
     * Returns a null QImage by default, which signals
     * @c canvas::render_to_image() to fall back to the QPainter-based
     * deferred path. Backends that support GPU readback (e.g.
     * @ref rhi_backend) override this.
     *
     * @param w  Desired output width  (0 = use the widget's current width).
     * @param h  Desired output height (0 = use the widget's current height).
     */
    virtual QImage render_to_image(int /*w*/, int /*h*/) { return {}; }
};

} // namespace ezgl
