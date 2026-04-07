#version 440

layout(location = 0) in vec2 inPosition;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
} ubo;

void main()
{
    gl_Position = ubo.mvp * vec4(inPosition, 0.0, 1.0);
}
