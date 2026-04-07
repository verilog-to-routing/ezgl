#version 440

layout(std140, binding = 1) uniform draw_buf {
    vec4 color;
} draw_ubo;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = draw_ubo.color;
}
