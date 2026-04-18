#version 440

layout(location = 0) in float v_t;
layout(location = 1) flat in float v_line_len_px;
layout(location = 2) flat in float v_phase_px;

layout(std140, binding = 1) uniform style_buf {
    vec4 color;
    vec4 line; // x: width_px, y: dash_px, z: gap_px, w: unused
} style;

layout(location = 0) out vec4 fragColor;

void main()
{
    // Screen-pixel distance from the original unclipped segment start.
    float dist_px = v_phase_px + v_t * v_line_len_px;

    // Discard fragments that fall in the gap.
    float dash_px = style.line.y;
    float gap_px = style.line.z;
    float period = dash_px + gap_px;
    if (period > 1e-3) {
        float phase = mod(dist_px, period);
        if (phase >= dash_px)
            discard;
    }

    fragColor = style.color;
}
