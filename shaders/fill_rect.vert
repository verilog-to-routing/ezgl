#version 440

layout(location = 0) in vec2 inMin;
layout(location = 1) in vec2 inMax;

layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec2 viewport;
} ubo;

void main()
{
    float x = ((gl_VertexIndex & 1) == 0) ? inMin.x : inMax.x;
    float y = ((gl_VertexIndex & 2) == 0) ? inMin.y : inMax.y;
    gl_Position = ubo.mvp * vec4(x, y, 0.0, 1.0);
}
