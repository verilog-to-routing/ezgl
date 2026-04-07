#version 440

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec4 inColor;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
} ubo;

layout(location = 0) out vec4 v_color;

void main()
{
    gl_Position = ubo.mvp * vec4(inPosition, 0.0, 1.0);
    v_color = inColor;
}
