#version 460 core
#include "common.glsl"

PS_OUT(0) vec4 outColor;

void main()
{
    float z = gl_FragCoord.z;
    outColor = vec4(z, z, z, 1.0);
}
