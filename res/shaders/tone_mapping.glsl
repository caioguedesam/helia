#version 460 core
#include "common.glsl"

// ====================================================
#ifdef VERTEX_SHADER
VS_IN(0) vec2 inPosition;
VS_IN(1) vec2 inUV;

VS_OUT(0) vec2 outUV;

void main()
{
    gl_Position = vec4(inPosition, 0.0, 1.0);
    outUV = inUV;
}
#endif // VERTEX_SHADER

// ====================================================
#ifdef PIXEL_SHADER

PS_IN(0) vec2 inUV;

PS_OUT(0) vec4 outColor;

vec3 ToneMapping_ACES(vec3 x)
{
    // Approximated ACES fit
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}

vec3 ToneMapping_HablePartial(vec3 x)
{
    float A = 0.15f;
    float B = 0.50f;
    float C = 0.10f;
    float D = 0.20f;
    float E = 0.02f;
    float F = 0.30f;
    return ((x*(A*x+C*B)+D*E)/(x*(A*x+B)+D*F))-E/F;
}

vec3 ToneMapping_Hable(vec3 v)
{
    float exposure_bias = 2.0f;
    vec3 curr = ToneMapping_HablePartial(v * exposure_bias);

    vec3 W = vec3(11.2f);
    vec3 white_scale = vec3(1.0f) / ToneMapping_HablePartial(W);
    return curr * white_scale;
}


void main()
{
    vec4 result = texture(
            sampler2D(lightingAccum, samplerPoint), inUV);

    result.rgb = ToneMapping_ACES(result.rgb);
    //result.rgb = ToneMapping_Hable(result.rgb);
    
    // Gamma correction
    result.rgb = pow(result.rgb, vec3(1.0 / 2.2));

    outColor = vec4(result.rgb, 1.0);
}

#endif // PIXEL_SHADER
