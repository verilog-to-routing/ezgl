#version 440

layout(location = 0) in vec2 inPosition;
layout(location = 1) in float inStyleNorm;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
} ubo;

layout(location = 0) out float v_style_index;

void main()
{
    gl_Position = ubo.mvp * vec4(inPosition, 0.0, 1.0);
    v_style_index = inStyleNorm * 255.0;
}
