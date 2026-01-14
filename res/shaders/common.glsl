#include "macros.glsl"

struct SceneMesh
{
    uint mIndexStart;
    uint mIndexCount;
};

struct SceneNode
{
    mat4 mTransform;
    uint mMeshId;
};

struct PerFrameUniforms
{
    mat4 mWorld;
    mat4 mView;
    mat4 mProj;
};
