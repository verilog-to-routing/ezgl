#version 440

layout(location = 0) in vec2 inPosition;
layout(location = 1) in vec2 inTexCoord;

layout(location = 0) out vec2 v_tex_coord;

void main()
{
    gl_Position = vec4(inPosition, 0.0, 1.0);
    v_tex_coord = inTexCoord;
}
