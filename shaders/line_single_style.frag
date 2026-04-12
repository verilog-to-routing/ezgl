#version 440

layout(std140, binding = 1) uniform line_color_buf {
    vec4 color;
} line_color;

layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = line_color.color;
}
