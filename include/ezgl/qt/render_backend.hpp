#pragma once

#include "ezgl/irenderer.hpp"

namespace ezgl {

using draw_canvas_fn = void (*)(renderer*);

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
};

} // namespace ezgl
