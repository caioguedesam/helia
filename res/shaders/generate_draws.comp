#version 460 core
#include "common.glsl"

layout(local_size_x = 32, local_size_y = 1, local_size_z = 1) in;

DEFINE_CONSTANT_BLOCK
{
    uint nodeCount;
};

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
