#version 440

layout(binding = 0) uniform sampler2D overlayTex;

layout(location = 0) in vec2 v_tex_coord;
layout(location = 0) out vec4 fragColor;

void main()
{
    fragColor = texture(overlayTex, v_tex_coord);
}
