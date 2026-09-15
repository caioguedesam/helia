#include "common.slang"

struct Params
{
    uint nodeCount;
    uint pFrame;
    uint pDrawBuffers;
    uint pDrawCounts;
    uint pInstancesShadow;
    uint pInstancesOpaque;
    uint pInstancesOpaqueDoubleSided;
    uint pNodes;
    uint pMeshes;
    uint pMaterials;
};

void FrustumPlanesFromViewProj(float4x4 vp, out float4 planes[6])
{
    // https://www.gamedevs.org/uploads/fast-extraction-viewing-frustum-planes-from-world-view-projection-matrix.pdf
    // Note: matrix is transposed here to account for column-major multiplication ordering
    // (Mv instead of vM).

    // Extract rows from column-major matrix
    // HLSL accesses are M[row][column]
    float4 r0 = vp[0];
    float4 r1 = vp[1];
    float4 r2 = vp[2];
    float4 r3 = vp[3];

    // Old GLSL code (accesses are M[column][row]
    //float4 r0 = float4(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
    //float4 r1 = float4(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
    //float4 r2 = float4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
    //float4 r3 = float4(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

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
bool CameraFrustumTest(SceneNode node, PerFrame frame)
{
    // AABB is in frustum if, for each plane, the point furthest along the plane's normal
    // is inside it's half-space.
    float4 planes[6];
    FrustumPlanesFromViewProj(mul(frame.mMainProj, frame.mMainView), planes);

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

bool ShadowCascadeFrustumTest(SceneNode node, PerFrame frame, uint cascade)
{
    if(cascade >= MAX_CASCADES)
    {
        return true;
    }

    float4 planes[6];
    FrustumPlanesFromViewProj(frame.mShadowCascadesViewProj[cascade], planes);

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

bool OcclusionTest(SceneNode node, PerFrame frame)
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
        p[i] = mul(mul(frame.mMainProj, frame.mMainView), p[i]);
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
    RWTexture2D<float> hizBase = GetRWTexture2DFloat(frame.mHandleHiZ[0]);
    hizBase.GetDimensions(baseWidth, baseHeight);
    float2 aabbWidth = xyMax - xyMin;
    float2 texelSize = aabbWidth * float2(baseWidth, baseHeight);

    // Get the mip where AABB covers 4 texels (one for each corner)
    // TODO(caio): Still get some objects being occluded when they shouldn't here.
    // Is the mip selected not the 2x2 footprint for the AABB?
    int mip = int(min(floor(log2(max(texelSize.x, texelSize.y))) + 1.0, HIZ_MAX - 1.0));
    uint mipWidth, mipHeight;
    RWTexture2D<float> hizMip = GetRWTexture2DFloat(frame.mHandleHiZ[mip]);
    hizMip.GetDimensions(mipWidth, mipHeight);
    float2 mipSize = float2(mipWidth, mipHeight);

    float4 box = float4(xyMin, xyMax);

    float z0 = hizMip[int2(mipSize * box.xy)].r;
    float z1 = hizMip[int2(mipSize * box.zw)].r;
    float z2 = hizMip[int2(mipSize * box.xw)].r;
    float z3 = hizMip[int2(mipSize * box.zy)].r;

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

CS_ENTRY
[numthreads(32, 1, 1)]
void CSMain(uint3 id : SV_DispatchThreadID, uniform Params params)
{
    uint idx = id.x;
    if(idx >= params.nodeCount)
    {
        return;
    }

    PerFrame frame          = GetBufferConstants(PerFrame, params.pFrame);
    SceneNode node          = GetBuffer(SceneNode, params.pNodes)[idx];
    SceneMesh mesh          = GetBuffer(SceneMesh, params.pMeshes)[node.mMeshId];
    SceneMaterial material  = GetBuffer(SceneMaterial, params.pMaterials)[node.mMaterialId];

#if SHADOW_MAP
    // Shadow pass
    for(int i = 0; i < MAX_CASCADES; i++)
    {
        uint drawBuffer = DB_SHADOW_0 + i;
        if(ShadowCascadeFrustumTest(node, frame, i))
        {
            uint drawIdx;
            InterlockedAdd(GetBuffer(uint, params.pDrawCounts)[drawBuffer], 1, drawIdx);
            
            IndirectDraw draw;
            draw.mIndexCount = mesh.mIndexCount;
            draw.mInstanceCount = 1;
            draw.mFirstIndex = mesh.mIndexStart;
            draw.mVertexOffset = mesh.mVertexOffset;
            draw.mFirstInstance = 0;

            uint drawBufferOffset = drawBuffer * MAX_DRAWS + drawIdx;
            GetBuffer(IndirectDraw, params.pDrawBuffers)[drawBufferOffset] = draw;

            GetBuffer(InstanceData, params.pInstancesShadow)[(i * MAX_DRAWS) + drawIdx].mNodeId = idx;
        }
    }
#else
    // Frustum culling
    if(!CameraFrustumTest(node, frame))
    {
        return;
    }
    
    // Occlusion culling with Hi-Z
    if(!OcclusionTest(node, frame))
    {
        return;
    }

    uint drawBuffer = (material.mDoubleSided == 1) ? DB_GBUFFER_OPAQUE_DOUBLE : DB_GBUFFER_OPAQUE;
    uint drawIdx;
    InterlockedAdd(GetBuffer(uint, params.pDrawCounts)[drawBuffer], 1, drawIdx);
    IndirectDraw draw;
    draw.mIndexCount = mesh.mIndexCount;
    draw.mInstanceCount = 1;
    draw.mFirstIndex = mesh.mIndexStart;
    draw.mVertexOffset = mesh.mVertexOffset;
    draw.mFirstInstance = 0;

    uint drawBufferOffset = drawBuffer * MAX_DRAWS + drawIdx;
    GetBuffer(IndirectDraw, params.pDrawBuffers)[drawBufferOffset] = draw;

    if(material.mDoubleSided == 1)
    {
        GetBuffer(InstanceData, params.pInstancesOpaqueDoubleSided)[drawIdx].mNodeId = idx;
    }
    else
    {
        GetBuffer(InstanceData, params.pInstancesOpaque)[drawIdx].mNodeId = idx;
    }
#endif
}
