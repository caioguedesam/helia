#version 460 core
#include "common.glsl"

DEFINE_CONSTANT_BLOCK
{
    uint frameId;
};

// Normal mapping can opt in to precomputed tangents. Useful for checking if model has proper tangents.
#define VERTEX_TANGENTS 1

// ====================================================
#ifdef VERTEX_SHADER
VS_IN(0) vec3 inPosition;
VS_IN(1) vec3 inNormal;
VS_IN(2) vec2 inUV;
VS_IN(3) vec4 inTangent;

VS_OUT_NOINTERP(0) uint outNodeId;
VS_OUT(1) vec2 outUV;
VS_OUT(2) vec3 outNormal;
VS_OUT(3) vec3 outWorldPos;
#if VERTEX_TANGENTS
VS_OUT(4) vec3 outTangent;
VS_OUT(5) vec3 outBitangent;
#endif

void main()
{
    outUV = inUV;

    PerDrawData drawData = perDraw[gl_DrawID];
#if DOUBLE_SIDED
    outNodeId = drawData.mNodeIdOpaqueDoubleSided;
#else
    outNodeId = drawData.mNodeIdOpaque;
#endif
    SceneNode node = sceneNodes[outNodeId];
    PerFrameUniforms perFrame = perFrameUniforms[frameId];
    vec4 worldPos = node.mTransform * vec4(inPosition, 1.0f);
    gl_Position = perFrame.mProj * perFrame.mView * worldPos;

    mat3 mN = transpose(inverse(mat3(node.mTransform)));
    outNormal = normalize(mN * inNormal);
    outWorldPos = worldPos.xyz;

#if VERTEX_TANGENTS
    outTangent = normalize(mN * inTangent.xyz);
    outBitangent = cross(outNormal, outTangent) * inTangent.w;
#endif
}
#endif  // VERTEX_SHADER

// ====================================================
#ifdef PIXEL_SHADER
PS_IN_NOINTERP(0) uint inNodeId;
PS_IN(1) vec2 inUV;
PS_IN(2) vec3 inNormal;
PS_IN(3) vec3 inWorldPos;
#if VERTEX_TANGENTS
PS_IN(4) vec3 inTangent;
PS_IN(5) vec3 inBitangent;
#endif

PS_OUT(0) vec4 outGBufferA;
PS_OUT(1) vec4 outGBufferB;
PS_OUT(2) vec2 outGBufferC;

mat3 getTBN( vec3 N, vec3 p, vec2 uv )
{
    // get edge vectors of the pixel triangle
    vec3 dp1 = dFdx( p );
    vec3 dp2 = dFdy( p );
    vec2 duv1 = dFdx( uv );
    vec2 duv2 = dFdy( uv );

    // solve the linear system
    vec3 dp2perp = cross( dp2, N );
    vec3 dp1perp = cross( N, dp1 );
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // construct a scale-invariant frame 
    float invMax = inversesqrt( max( dot(T,T), dot(B,B) ) );
    T *= invMax;
    B *= invMax;

    return mat3( T, B, N );
}

void main()
{
    SceneNode node = sceneNodes[inNodeId];
    SceneMaterial material = sceneMaterials[node.mMaterialId];
    PerFrameUniforms perFrame = perFrameUniforms[frameId];

    // Diffuse color
    vec4 baseColor = texture(
            sampler2D(materialMaps[material.mBaseColorTexId], samplerLinear),
            inUV);
    float mask = step(material.mAlphaCutoff, baseColor.a);
    if(mask < 1.0)
    {
        discard;
    }

    outGBufferA = vec4(baseColor.rgb * material.mBaseColor.rgb, 1.0);

    // Normals
    vec3 vertexNormal = inNormal;
#if VERTEX_TANGENTS
    vec3 vertexTangent = inTangent;
#endif
#if DOUBLE_SIDED
    if(!gl_FrontFacing)
    {
        vertexNormal = -vertexNormal;
#if VERTEX_TANGENTS
        vertexTangent = -vertexTangent;
#endif
    }
#endif

#if VERTEX_TANGENTS
    mat3 TBN = mat3(vertexTangent, inBitangent, vertexNormal);
#else
    vec3 camPos = perFrame.mCamWorldPos.xyz;
    vec3 V = normalize(camPos - inWorldPos);
    mat3 TBN = getTBN(vertexNormal, -V, inUV);
#endif
    vec3 normalMap = texture(sampler2D(materialMaps[material.mNormalTexId], samplerLinear), inUV).rgb;
    normalMap = normalize(normalMap * 2.0f - 1.0f);
    vec3 normal = normalize(TBN * normalMap);
    outGBufferB = vec4(normal, 0.0);

    // Metallic + Roughness
    vec4 metallicRoughness = texture(
            sampler2D(materialMaps[material.mMetallicRoughnessTexId], samplerLinear),
            inUV);
    outGBufferC = vec2(
            metallicRoughness.g * material.mRoughness,
            metallicRoughness.b * material.mMetallic);
}
#endif  // PIXEL_SHADER
