#version 440

layout(location = 0) in float v_style_index;

layout(std140, binding = 1) uniform palette_buf {
    vec4 colors[256];
} palette;

layout(location = 0) out vec4 fragColor;

void main()
{
    int style_index = clamp(int(v_style_index + 0.5), 0, 255);
    fragColor = palette.colors[style_index];
}
