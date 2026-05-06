#version 440

// Per-instance: arrow anchor (world) + arrow direction (world; any length).
layout(location = 0) in vec2 inAnchor;
layout(location = 1) in vec2 inDir;

// Binding 0: MVP (64 B) + viewport (8 B).
layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec2 viewport;
} ubo;

// Binding 1: solid colour + line/style scalars. We reuse line.x as the
// arrow size in screen pixels (arrow tip-to-base distance × 2).
layout(std140, binding = 1) uniform style_buf {
    vec4 color;
    vec4 line; // x: arrow_size_px, y/z/w: unused for arrows
} style;

// Synthesise an arrow-head triangle pointing along inDir, anchored at
// inAnchor in world coords, drawn at a fixed SCREEN-pixel size from
// style.line.x. Three vertices per instance — corner picked via
// gl_VertexIndex (0=tip, 1=left base, 2=right base).
//
// Because tip/base offsets are computed in PIXEL space and then mapped
// to NDC (offset_px * 2 / viewport), the visual size of every arrow is
// constant at every zoom level. The shape is the same as VPR's
// historic CPU triangle:
//   tip  = anchor + dir_unit_px * r
//   base = anchor - dir_unit_px * r ± perp_unit_px * r
// where r = arrow_size_px / 2.
void main()
{
    // 1. Project anchor to clip space; record its NDC position.
    vec4 anchor_clip = ubo.mvp * vec4(inAnchor, 0.0, 1.0);
    vec2 anchor_ndc  = anchor_clip.xy / max(anchor_clip.w, 1e-6);

    // 2. Compute screen-pixel direction. Use a unit world step in inDir,
    //    project it through mvp, and scale to pixels via viewport/2.
    float dlen = length(inDir);
    vec2 dir_unit_world = (dlen > 1e-6) ? (inDir / dlen) : vec2(1.0, 0.0);
    vec4 tip_clip = ubo.mvp * vec4(inAnchor + dir_unit_world, 0.0, 1.0);
    vec2 tip_ndc  = tip_clip.xy / max(tip_clip.w, 1e-6);
    vec2 dir_px   = (tip_ndc - anchor_ndc) * (ubo.viewport * 0.5);
    float dir_px_len = length(dir_px);
    vec2 dir_unit_px = (dir_px_len > 1e-6) ? (dir_px / dir_px_len) : vec2(1.0, 0.0);
    vec2 perp_unit_px = vec2(-dir_unit_px.y, dir_unit_px.x);

    // 3. Pick corner offset in pixels (3 vertices per instance).
    float r = max(style.line.x, 1.0) * 0.5;
    int idx = gl_VertexIndex - 3 * (gl_VertexIndex / 3);
    vec2 offset_px;
    if (idx == 0) {
        offset_px = dir_unit_px * r;                                  // tip
    } else if (idx == 1) {
        offset_px = -dir_unit_px * r + perp_unit_px * r;              // left base
    } else {
        offset_px = -dir_unit_px * r - perp_unit_px * r;              // right base
    }

    // 4. Convert the pixel offset to NDC and add to the anchor's clip
    //    position. mvp here is orthographic (w == 1), so adding NDC
    //    directly to clip-space xy is correct without re-multiplying by w.
    vec2 ndc_offset = (offset_px * 2.0) / ubo.viewport;
    vec4 clip = anchor_clip;
    clip.xy += ndc_offset;
    gl_Position = clip;
}
