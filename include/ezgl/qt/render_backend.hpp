#pragma once

#include "ezgl/irenderer.hpp"

#include <QImage>

namespace ezgl {

using draw_canvas_fn = void (*)(renderer*);

enum class renderer_type { immediate, deferred, rhi };

inline constexpr const char* renderer_type_name(renderer_type t) noexcept
{
    switch (t) {
        case renderer_type::immediate: return "immediate";
        case renderer_type::deferred:  return "deferred";
        case renderer_type::rhi:       return "rhi";
    }
    return "unknown";
}

/**
 * Abstract rendering backend owned by canvas.
 *
 * Each concrete backend encapsulates one rendering path's full lifecycle:
 * frame scheduling, resize handling, and per-frame draw dispatch.
 * canvas selects the right implementation at initialize() time.
 */
class render_backend {
public:
    virtual ~render_backend() = default;

    virtual void redraw() = 0;
    virtual void redraw_camera_only() = 0;

    virtual void begin_deferred_redraw_cycle() {}
    virtual void end_deferred_redraw_cycle() {}

    virtual void on_pre_resize() {}
    virtual void on_resize(int w, int h) = 0;

    virtual renderer* create_animation_renderer() = 0;

    /**
     * Render a frame and return it as a QImage.
     *
     * Returns a null QImage by default, which signals canvas::render_to_image()
     * to fall back to the QPainter-based deferred path.
     * Backends that support GPU readback (e.g. rhi_backend) override this.
     *
     * @param w  Desired output width  (0 = use the widget's current width).
     * @param h  Desired output height (0 = use the widget's current height).
     */
    virtual QImage render_to_image(int /*w*/, int /*h*/) { return {}; }
};

} // namespace ezgl
