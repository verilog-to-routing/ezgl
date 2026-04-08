#version 440

layout(location = 0) in float v_style_index;
layout(location = 1) in float v_t;
layout(location = 2) flat in float v_line_len_px;
layout(location = 3) flat in float v_dash_px;
layout(location = 4) flat in float v_gap_px;
layout(location = 5) flat in float v_phase_px;

layout(std140, binding = 1) uniform palette_buf {
    vec4 colors[256];
} palette;

layout(location = 0) out vec4 fragColor;

void main()
{
    // Screen-pixel distance from the original unclipped segment start.
    float dist_px = v_phase_px + v_t * v_line_len_px;

    // Discard fragments that fall in the gap.
    float period = v_dash_px + v_gap_px;
    if (period > 1e-3) {
        float phase = mod(dist_px, period);
        if (phase >= v_dash_px)
            discard;
    }

    int style_index = clamp(int(v_style_index + 0.5), 0, 255);
    fragColor = palette.colors[style_index];
}
