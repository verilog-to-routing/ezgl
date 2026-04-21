#version 440

// ---- Per-vertex (4 quad corners, constant buffer) ---------------------------
// t    : 0.0 = at line start, 1.0 = at line end
// side : -1.0 = left edge,   +1.0 = right edge
layout(location = 0) in vec2  inCorner;    // (t, side)

// ---- Per-instance (one record per thick-line segment) -----------------------
layout(location = 1) in vec2  inStart;     // world-space start point
layout(location = 2) in vec2  inEnd;       // world-space end point

// Binding 0: MVP (64 B) + viewport vec2 (8 B) = 72 B used, buffer is 80 B.
layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec2 viewport;
} ubo;

layout(std140, binding = 1) uniform style_buf {
    vec4 color;
    vec4 line; // x: width_px, y: dash_px, z: gap_px, w: unused
} style;

void main()
{
    float t    = inCorner.x;   // 0 or 1
    float side = inCorner.y;   // -1 or +1

    // World-space position along the centre line.
    vec2 pos = mix(inStart, inEnd, t);

    // ---- Perpendicular expansion in screen space ----------------------------
    //
    // World perp unit vector (rotate direction by 90°).
    vec2 dir = inEnd - inStart;
    float dir_len = length(dir);
    vec2 perp;
    if (dir_len > 1e-6) {
        perp = vec2(-dir.y, dir.x) / dir_len;
    } else {
        perp = vec2(0.0, 1.0); // degenerate: arbitrary perp
    }

    // Map world perp to screen pixels:
    //   mvp[0][0] = 2*sx/fw,  mvp[1][1] = 2*sy/fh  (column-major GLSL)
    //   sx = pixels-per-world-unit in x,  sy = same in y
    vec2 screen_perp = vec2(
        perp.x * ubo.mvp[0][0] * (ubo.viewport.x * 0.5),
        perp.y * ubo.mvp[1][1] * (ubo.viewport.y * 0.5));

    float sp_len = length(screen_perp);
    if (sp_len > 1e-6)
        screen_perp /= sp_len;          // unit vector in screen-pixel space

    // Offset by half-width pixels → NDC:  ndc = screen * 2 / viewport
    float width_px = max(style.line.x, 1.0);
    vec2 ndc_offset = side * screen_perp * (width_px / ubo.viewport);

    vec4 clip = ubo.mvp * vec4(pos, 0.0, 1.0);
    clip.xy += ndc_offset;
    gl_Position = clip;
}
