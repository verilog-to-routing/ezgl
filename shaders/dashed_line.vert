#version 440

// ---- Per-vertex (4 quad corners, same constant buffer as thick_line) --------
layout(location = 0) in vec2  inCorner;    // (t, side)

// ---- Per-instance (one record per dashed-line segment) ----------------------
layout(location = 1) in vec2  inStart;     // world-space start
layout(location = 2) in vec2  inEnd;       // world-space end
layout(location = 3) in float inPhaseWorld; // world-space distance from original line start to this segment

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec2 viewport;
} ubo;

layout(std140, binding = 1) uniform style_buf {
    vec4 color;
    vec4 line; // x: width_px, y: dash_px, z: gap_px, w: unused
} style;

// v_t and v_line_len_px together let the fragment shader compute the
// screen-pixel distance from the original segment start for each fragment.
// v_t is interpolated; the rest are flat (constant per quad).
layout(location = 0) out float v_t;
layout(location = 1) flat out float v_line_len_px;
layout(location = 2) flat out float v_phase_px;

void main()
{
    float t    = inCorner.x;
    float side = inCorner.y;

    vec2 pos = mix(inStart, inEnd, t);

    // ---- Perpendicular expansion (same math as thick_line.vert) -------------
    vec2 dir = inEnd - inStart;
    float dir_len = length(dir);
    vec2 perp;
    if (dir_len > 1e-6) {
        perp = vec2(-dir.y, dir.x) / dir_len;
    } else {
        perp = vec2(0.0, 1.0);
    }

    vec2 screen_perp = vec2(
        perp.x * ubo.mvp[0][0] * (ubo.viewport.x * 0.5),
        perp.y * ubo.mvp[1][1] * (ubo.viewport.y * 0.5));
    float sp_len = length(screen_perp);
    if (sp_len > 1e-6)
        screen_perp /= sp_len;

    float width_px = max(style.line.x, 1.0);
    vec2 ndc_offset = side * screen_perp * (width_px / ubo.viewport);

    vec4 clip = ubo.mvp * vec4(pos, 0.0, 1.0);
    clip.xy += ndc_offset;
    gl_Position = clip;

    // ---- Dash varyings -------------------------------------------------------
    // Use screen-pixel dash lengths, but preserve continuous phase across
    // clipped segments by converting the stored world-phase using the current
    // screen scale of the segment.
    vec4 clip_start = ubo.mvp * vec4(inStart, 0.0, 1.0);
    vec4 clip_end   = ubo.mvp * vec4(inEnd,   0.0, 1.0);
    vec2 screen_delta = (clip_end.xy - clip_start.xy) * (ubo.viewport * 0.5);
    float line_len_px = length(screen_delta);
    float line_len_world = length(inEnd - inStart);
    float phase_px = 0.0;
    if (line_len_world > 1e-6)
        phase_px = inPhaseWorld * (line_len_px / line_len_world);

    v_line_len_px = line_len_px;
    v_t           = t;
    v_phase_px    = phase_px;
}
