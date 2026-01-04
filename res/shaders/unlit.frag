#version 460 core

layout(location = 0) in vec2 voTexCoord;

layout(location = 0) out vec4 poColor;

layout(set = 0, binding = 1) uniform texture2D texDefault;
layout(set = 0, binding = 2) uniform sampler samplerDefault;

void main()
{
    //poColor = vec4(1.0, 0.0, 1.0, 1.0);
    poColor = texture(sampler2D(texDefault, samplerDefault), voTexCoord);
}
