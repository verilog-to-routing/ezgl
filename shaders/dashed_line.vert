#version 440

// ---- Per-vertex (4 quad corners, same constant buffer as thick_line) --------
layout(location = 0) in vec2  inCorner;    // (t, side)

// ---- Per-instance (one record per dashed-line segment) ----------------------
layout(location = 1) in vec2  inStart;     // world-space start
layout(location = 2) in vec2  inEnd;       // world-space end
layout(location = 3) in float inWidthPx;   // full line width in screen pixels (>= 1)
layout(location = 4) in float inDashPx;    // dash length in screen pixels
layout(location = 5) in float inGapPx;     // gap length in screen pixels
layout(location = 6) in float inStyleNorm; // palette index, normalised 0-1

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec2 viewport;
} ubo;

// v_t and v_line_len_px together let the fragment shader compute the
// screen-pixel distance from the segment start for each fragment.
// v_t is interpolated; the rest are flat (constant per quad).
layout(location = 0) out float v_style_index;
layout(location = 1) out float v_t;
layout(location = 2) flat out float v_line_len_px;
layout(location = 3) flat out float v_dash_px;
layout(location = 4) flat out float v_gap_px;

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

    vec2 ndc_offset = side * screen_perp * (inWidthPx / ubo.viewport);

    vec4 clip = ubo.mvp * vec4(pos, 0.0, 1.0);
    clip.xy += ndc_offset;
    gl_Position = clip;

    // ---- Dash varyings -------------------------------------------------------
    // Compute total line length in screen pixels so the fragment shader can
    // turn t (0-1) into an absolute pixel distance from the segment start.
    vec4 clip_start = ubo.mvp * vec4(inStart, 0.0, 1.0);
    vec4 clip_end   = ubo.mvp * vec4(inEnd,   0.0, 1.0);
    // For orthographic projection clip.w == 1, so clip.xy == ndc.xy.
    vec2 screen_delta = (clip_end.xy - clip_start.xy) * (ubo.viewport * 0.5);
    v_line_len_px = length(screen_delta);

    v_t           = t;
    v_dash_px     = inDashPx;
    v_gap_px      = inGapPx;
    v_style_index = inStyleNorm * 255.0;
}
