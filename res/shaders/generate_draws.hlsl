#include "common.hlsl"

PUSH_CONSTANTS_BEGIN()
uint nodeCount;
PUSH_CONSTANTS_END()

void FrustumPlanesFromViewProj(float4x4 vp, out float4 planes[6])
{
    // https://www.gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdf
    // Note: matrix is transposed here to account for column-major multiplication ordering
    // (Mv instead of vM).

    // Extract rows from column-major matrix
    float4 r0 = float4(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
    float4 r1 = float4(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
    float4 r2 = float4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    float4 r3 = float4(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

    // Plane: dot(n, p) + d >= 0
    planes[0] = r3 + r0; // Left
    planes[1] = r3 - r0; // Right
    planes[2] = r3 + r1; // Bottom
    planes[3] = r3 - r1; // Top
    planes[4] = r3 - r2; // Near
    planes[5] = r2;      // Far

    // Normalize
    for (int i = 0; i < 6; i++)
    {
        float len = length(planes[i].xyz);
        planes[i] /= len;
    }
}

// Frustum culling
bool CameraFrustumTest(SceneNode node, PerFrameUniforms perFrame)
{
    // AABB is in frustum if, for each plane, the point furthest along the plane's normal
    // is inside it's half-space.
    float4 planes[6];
    FrustumPlanesFromViewProj(mul(perFrame.mMainProj, perFrame.mMainView), planes);

    for(int i = 0; i < 6; i++)
    {
        //float4 plane = perFrame.mCameraFrustumPlanes[i];
        float4 plane = planes[i];
        float3 p;

        p.x = plane.x < 0 ? node.mMinAABB.x : node.mMaxAABB.x;
        p.y = plane.y < 0 ? node.mMinAABB.y : node.mMaxAABB.y;
        p.z = plane.z < 0 ? node.mMinAABB.z : node.mMaxAABB.z;

        float sdf = dot(plane.xyz, p) + plane.w;
        if(sdf < 0)
        {
            return false;
        }
    }
    return true;
}

bool ShadowCascadeFrustumTest(SceneNode node, PerFrameUniforms perFrame, uint cascade)
{
    if(cascade >= MAX_CASCADES)
    {
        return true;
    }

    float4 planes[6];
    FrustumPlanesFromViewProj(perFrame.mShadowCascadesViewProj[cascade], planes);

    // AABB is in frustum if, for each plane, the point furthest along the plane's normal
    // is inside it's half-space.
    for(int i = 0; i < 6; i++)
    {
        float4 plane = planes[i];
        float3 p;

        p.x = plane.x < 0 ? node.mMinAABB.x : node.mMaxAABB.x;
        p.y = plane.y < 0 ? node.mMinAABB.y : node.mMaxAABB.y;
        p.z = plane.z < 0 ? node.mMinAABB.z : node.mMaxAABB.z;

        float sdf = dot(plane.xyz, p) + plane.w;
        if(sdf < 0)
        {
            return false;
        }
    }
    return true;
}

bool IsPointInNDC(float3 p)
{
    return !(p.x < -1.0 || p.x > 1.0 || p.y < -1.0 || p.y > 1.0 || p.z < 0.0 || p.z > 1.0);
}

bool OcclusionTest(SceneNode node, PerFrameUniforms perFrame)
{
    // Project 8 aabb corners to Z buffer UV space
    float4 p[8];
    p[0] = float4(node.mMinAABB.x, node.mMinAABB.y, node.mMinAABB.z, 1);
    p[1] = float4(node.mMaxAABB.x, node.mMinAABB.y, node.mMinAABB.z, 1);
    p[2] = float4(node.mMinAABB.x, node.mMaxAABB.y, node.mMinAABB.z, 1);
    p[3] = float4(node.mMaxAABB.x, node.mMaxAABB.y, node.mMinAABB.z, 1);
    p[4] = float4(node.mMinAABB.x, node.mMinAABB.y, node.mMaxAABB.z, 1);
    p[5] = float4(node.mMaxAABB.x, node.mMinAABB.y, node.mMaxAABB.z, 1);
    p[6] = float4(node.mMinAABB.x, node.mMaxAABB.y, node.mMaxAABB.z, 1);
    p[7] = float4(node.mMaxAABB.x, node.mMaxAABB.y, node.mMaxAABB.z, 1);
    float2 xyMin = float2(FLT_MAX, FLT_MAX);
    float2 xyMax = float2(-FLT_MAX, -FLT_MAX);
    // Track closest point of the AABB corners to the camera (reverse Z = max)
    float zMax = 0.f;

    for(int i = 0; i < 8; i++)
    {
        // Convert to clip space
        // TODO(caio): Change this to premultiplied view proj
        p[i] = mul(mul(perFrame.mMainProj, perFrame.mMainView), p[i]);
        p[i] = p[i] / p[i].w;

        // Early out: if any AABB corner is out of bounds, don't test.
        if(!IsPointInNDC(p[i].xyz))
        {
            return true;
        }

        // Convert to UV space
        p[i] = float4((p[i].x + 1.0) / 2.0, (p[i].y + 1.0) / 2.0, p[i].z, 1.0);

        xyMin = min(xyMin, p[i].xy);
        xyMax = max(xyMax, p[i].xy);
        zMax = max(zMax, p[i].z);
    }

    // Get the width/height of AABB in texels of first Hi-Z buffer.
    uint baseWidth, baseHeight;
    GetStorageTextureR32F(perFrame.mHandleHiZ[0]).GetDimensions(baseWidth, baseHeight);
    float2 aabbWidth = xyMax - xyMin;
    float2 texelSize = aabbWidth * float2(baseWidth, baseHeight);

    // Get the mip where AABB covers 4 texels (one for each corner)
    // TODO(caio): Still get some objects being occluded when they shouldn't here.
    // Is the mip selected not the 2x2 footprint for the AABB?
    int mip = int(min(floor(log2(max(texelSize.x, texelSize.y))) + 1.0, HIZ_MAX - 1.0));
    uint mipWidth, mipHeight;
    GetStorageTextureR32F(perFrame.mHandleHiZ[mip]).GetDimensions(mipWidth, mipHeight);
    float2 mipSize = float2(mipWidth, mipHeight);

    float4 box = float4(xyMin, xyMax);

    float z0 = GetStorageTextureR32F(perFrame.mHandleHiZ[mip])[int2(mipSize * box.xy)].r;
    float z1 = GetStorageTextureR32F(perFrame.mHandleHiZ[mip])[int2(mipSize * box.zw)].r;
    float z2 = GetStorageTextureR32F(perFrame.mHandleHiZ[mip])[int2(mipSize * box.xw)].r;
    float z3 = GetStorageTextureR32F(perFrame.mHandleHiZ[mip])[int2(mipSize * box.zy)].r;

    //-float z0 = imageLoad(getStorageTexture(perFrame.mHandleHiZ[mip]), ifloat2(mipSize * box.xy)).r;
    //-float z1 = imageLoad(getStorageTexture(perFrame.mHandleHiZ[mip]), ifloat2(mipSize * box.zw)).r;
    //-float z2 = imageLoad(getStorageTexture(perFrame.mHandleHiZ[mip]), ifloat2(mipSize * box.xw)).r;
    //-float z3 = imageLoad(getStorageTexture(perFrame.mHandleHiZ[mip]), ifloat2(mipSize * box.zy)).r;

    // Get furthest occluder stored in hiz mip (reverse Z = min)
    float hizMin = min(z0, min(z1, min(z2, z3)));

    // AABB is fully occluded if closest point is behind furthest occluder (reverse Z = aabb z < occluder z).
    float bias = 1e-7f;
    return zMax >= (hizMin - bias);
}

[numthreads(32, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID)
{
    uint idx = id.x;
    if(idx >= pushConstants.nodeCount)
    {
        return;
    }

    SceneNode node = sceneNodes[idx];
    SceneMesh mesh = sceneMeshes[node.mMeshId];
    SceneMaterial mat = sceneMaterials[node.mMaterialId];

#if SHADOW_MAP
    // Shadow pass
    for(int i = 0; i < MAX_CASCADES; i++)
    {
        uint drawBuffer = DB_SHADOW_0 + i;
        if(ShadowCascadeFrustumTest(node, perFrame, i))
        {
            uint drawIdx;
            InterlockedAdd(drawCounts[drawBuffer], 1, drawIdx);
            
            IndirectDraw draw;
            draw.mIndexCount = mesh.mIndexCount;
            draw.mInstanceCount = 1;
            draw.mFirstIndex = mesh.mIndexStart;
            draw.mVertexOffset = mesh.mVertexOffset;
            draw.mFirstInstance = 0;

            uint drawBufferOffset = drawBuffer * MAX_DRAWS + drawIdx;
            drawBuffers[drawBufferOffset] = draw;

            instancesShadow[(i * MAX_DRAWS) + drawIdx].mNodeId = idx;
        }
    }
#else
    // Frustum culling
    if(!CameraFrustumTest(node, perFrame))
    {
        return;
    }
    
    // Occlusion culling with Hi-Z
    if(!OcclusionTest(node, perFrame))
    {
        return;
    }

    uint drawBuffer = (mat.mDoubleSided == 1) ? DB_GBUFFER_OPAQUE_DOUBLE : DB_GBUFFER_OPAQUE;
    uint drawIdx;
    InterlockedAdd(drawCounts[drawBuffer], 1, drawIdx);
    IndirectDraw draw;
    draw.mIndexCount = mesh.mIndexCount;
    draw.mInstanceCount = 1;
    draw.mFirstIndex = mesh.mIndexStart;
    draw.mVertexOffset = mesh.mVertexOffset;
    draw.mFirstInstance = 0;

    uint drawBufferOffset = drawBuffer * MAX_DRAWS + drawIdx;
    drawBuffers[drawBufferOffset] = draw;

    if(mat.mDoubleSided == 1)
    {
        instancesOpaqueDouble[drawIdx].mNodeId = idx;
    }
    else
    {
        instancesOpaque[drawIdx].mNodeId = idx;
    }
#endif
}
