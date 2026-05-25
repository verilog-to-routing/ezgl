#version 440

layout(std140, binding = 1) uniform style_buf {
    vec4 color;
    vec4 line; // x: width_px, y: dash_px, z: gap_px, w: unused
} style;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = style.color;
}
