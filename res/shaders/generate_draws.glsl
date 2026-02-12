#version 460 core
#include "common.glsl"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

DEFINE_CONSTANT_BLOCK
{
    uint nodeCount;
    uint frameId;
};

// Frustum culling
bool inFrustum(SceneNode node, PerFrameUniforms perFrame)
{
    // AABB is in frustum if, for each plane, the point furthest along the plane's normal
    // is inside it's half-space.
    for(int i = 0; i < 6; i++)
    {
        vec4 plane = perFrame.mCameraFrustumPlanes[i];
        vec3 p;

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

void main()
{
    uint idx = gl_GlobalInvocationID.x;
    if(idx >= nodeCount)
    {
        return;
    }

    SceneNode node = sceneNodes[idx];
    SceneMesh mesh = sceneMeshes[node.mMeshId];
    SceneMaterial mat = sceneMaterials[node.mMaterialId];
    PerFrameUniforms perFrame = perFrameUniforms[frameId];

    // Frustum culling
    if(!inFrustum(node, perFrame))
    {
        return;
    }

    if(mat.mDoubleSided == 1)
    {
        uint drawIdx = atomicAdd(doubleSidedOpaqueDrawCount, 1);
        doubleSidedOpaqueDrawCmds[drawIdx].mIndexCount = mesh.mIndexCount;
        doubleSidedOpaqueDrawCmds[drawIdx].mInstanceCount = 1;
        doubleSidedOpaqueDrawCmds[drawIdx].mFirstIndex = mesh.mIndexStart;
        doubleSidedOpaqueDrawCmds[drawIdx].mVertexOffset = mesh.mVertexOffset;
        doubleSidedOpaqueDrawCmds[drawIdx].mFirstInstance = 0;
        perDraw[drawIdx].mNodeIdOpaqueDoubleSided = idx;
    }
    else
    {
        uint drawIdx = atomicAdd(opaqueDrawCount, 1);
        opaqueDrawCmds[drawIdx].mIndexCount = mesh.mIndexCount;
        opaqueDrawCmds[drawIdx].mInstanceCount = 1;
        opaqueDrawCmds[drawIdx].mFirstIndex = mesh.mIndexStart;
        opaqueDrawCmds[drawIdx].mVertexOffset = mesh.mVertexOffset;
        opaqueDrawCmds[drawIdx].mFirstInstance = 0;
        perDraw[drawIdx].mNodeIdOpaque = idx;
    }
}
