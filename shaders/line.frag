#version 440

layout(std140, binding = 1) uniform style_buf {
    vec4 color;
} style;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = style.color;
}
