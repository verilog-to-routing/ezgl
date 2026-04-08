#version 440

// Per-vertex data for one corner of a thick-line quad.
// The CPU emits 6 vertices per line (2 triangles).  Each vertex encodes the
// world-space endpoint it lives on, the shared perpendicular unit direction,
// the desired line width in screen pixels, and which side (+1/-1) of the
// centre line this corner is on.
layout(location = 0) in vec2  inPos;      // world-space endpoint (start or end)
layout(location = 1) in vec2  inPerp;     // normalized world-space perpendicular
layout(location = 2) in float inWidthPx;  // full line width in screen pixels
layout(location = 3) in float inSide;     // +1.0 or -1.0
layout(location = 4) in float inStyleNorm;// style-palette index, normalized 0-1

// Binding 0 is shared with line.vert: MVP matrix at offset 0 (64 bytes).
// The thick-line shader also reads the viewport size at offset 64 (8 bytes).
layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec2 viewport; // widget size in pixels (width, height)
} ubo;

layout(location = 0) out float v_style_index;

void main()
{
    // ---------- perpendicular expansion in screen space ----------------------
    //
    // For our 2-D orthographic MVP:
    //   mvp[col][row]  (GLSL column-major storage, matching QMatrix4x4::constData)
    //   mvp[0][0] = 2*sx/fw    — x scale world→NDC
    //   mvp[1][1] = 2*sy/fh    — y scale world→NDC
    //
    // Transform world perp direction to screen pixels (ignore translation):
    //   screen_perp = (inPerp.x * sx,  inPerp.y * sy)
    //               = (inPerp.x * mvp[0][0] * viewport.x/2,
    //                  inPerp.y * mvp[1][1] * viewport.y/2)
    vec2 screen_perp = vec2(
        inPerp.x * ubo.mvp[0][0] * (ubo.viewport.x * 0.5),
        inPerp.y * ubo.mvp[1][1] * (ubo.viewport.y * 0.5));

    float sp_len = length(screen_perp);
    if (sp_len > 1e-6)
        screen_perp /= sp_len;          // unit vector in screen-pixel space

    // Desired screen-space offset: screen_perp * (inWidthPx/2) * inSide
    // Convert to NDC (screen px → NDC: multiply by 2/viewport):
    //   ndc_offset = screen_perp * inWidthPx * inSide / viewport
    vec2 ndc_offset = inSide * screen_perp * (inWidthPx / ubo.viewport);

    // Apply offset to the clip-space position of this endpoint.
    // For ortho projection clip.w == 1, so clip.xy == ndc.xy.
    vec4 clip = ubo.mvp * vec4(inPos, 0.0, 1.0);
    clip.xy += ndc_offset;
    gl_Position = clip;

    v_style_index = inStyleNorm * 255.0;
}
