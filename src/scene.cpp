#include "scene.hpp"
#include "../dw/src/core/base.hpp"
#include "../dw/src/core/debug.hpp"
#include "../dw/src/core/memory.hpp"
#include "../dw/src/core/file.hpp"
#include "../dw/src/core/hash_map.hpp"
#include "../dw/src/render/texture.hpp"
#include "dw/src/math/volumes.hpp"

#define CGLTF_IMPLEMENTATION
#include "third_party/cgltf.h"

void initScene(Scene* pScene, uint64 arenaSize, uint64 stagingSize)
{
    ASSERT(pScene);
    *pScene = {};

    initArena(arenaSize, &pScene->mArena);
    initArena(stagingSize, &pScene->mTempArena);
}

void destroyScene(Scene* pScene)
{
    ASSERT(pScene);

    destroyArena(&pScene->mArena);
    destroyArena(&pScene->mTempArena);

    *pScene = {};
}

void* cgltfAlloc(void* user, cgltf_size size)
{
    Scene* pScene = (Scene*)user;
    void* ptr = arenaPushZero(&pScene->mTempArena, size);
    return ptr;
}

void cgltfFree(void* user, void* ptr)
{
    // Free is not relevant because of arena allocation strategy.
}

cgltf_result cgltfFileRead(
        const cgltf_memory_options* mo,
        const cgltf_file_options* fo,
        const char* path,
        cgltf_size* size,
        void** data)
{
    Scene* pScene = (Scene*)fo->user_data;
    uint64 fileSize = 0;
    byte* pFile = readFile(&pScene->mTempArena,
            str(path), &fileSize);

    *size = fileSize;
    *data = pFile;

    return cgltf_result_success;
}

void cgltfFileRelease(
        const cgltf_memory_options* mo,
        const cgltf_file_options* fo,
        void* data)
{
    // noop
}

bool isIndexAccessor(cgltf_accessor* pAccessor)
{
    return pAccessor->buffer_view->type == cgltf_buffer_view_type_indices
        && pAccessor->type == cgltf_type_scalar
        && pAccessor->component_type == cgltf_component_type_r_16u;
}

bool isVertexAccessor(cgltf_accessor* pAccessor)
{
    return pAccessor->buffer_view->type == cgltf_buffer_view_type_vertices
        && pAccessor->component_type == cgltf_component_type_r_32f;
}

int32 getAccessorBufferIndex(cgltf_accessor* pAccessor, cgltf_data* pGltfData)
{
    for(uint32 i = 0; i < pGltfData->buffers_count; i++)
    {
        if(pAccessor->buffer_view->buffer == &pGltfData->buffers[i])
        {
            return i;
        }
    }
    return -1;
}

void setupSceneModel(Scene* pScene, String modelPath)
{
    // Load model asset to scratch memory
    ARENA_CHECKPOINT_SET(&pScene->mTempArena, sceneModel);

    cgltf_options options = {};
    options.memory.user_data = (void*)pScene;
    options.memory.alloc_func = cgltfAlloc;
    options.memory.free_func = cgltfFree;
    options.file.user_data = (void*)pScene;
    options.file.read = cgltfFileRead;
    options.file.release = cgltfFileRelease;

    cgltf_data* pGltfData = NULL;
    cgltf_result result = cgltf_parse_file(&options, cstr(modelPath), &pGltfData);
    ASSERT(result == cgltf_result_success);

    result = cgltf_load_buffers(&options, pGltfData, cstr(modelPath));
    ASSERT(result == cgltf_result_success);

    result = cgltf_validate(pGltfData);
    ASSERT(result == cgltf_result_success);

    // Save texture indices in array so we can load them later
    HashMap<String, uint32> texInfoByPath = hashmap<String, uint32>(&pScene->mTempArena, SCENE_MAX_TEXTURES);
    // TODO(caio): Samplers
    for(cgltf_size t = 0; t < pGltfData->textures_count; t++)
    {
        cgltf_image* pTexImage = pGltfData->textures[t].image;
        MaterialTextureInfo info = {};
        info.mPath = str(pTexImage->uri);
        pScene->mTexInfos[t] = info;
        texInfoByPath.insert(info.mPath, t);
    }
    pScene->mTexCount = pGltfData->textures_count;

    // Create all scene materials
    for(cgltf_size m = 0; m < pGltfData->materials_count; m++)
    {
        cgltf_material* pMat = &pGltfData->materials[m];
        ASSERT(pMat->has_pbr_metallic_roughness);
        cgltf_pbr_metallic_roughness* pPBR = &pMat->pbr_metallic_roughness;

        SceneMaterial mat = {};
        mat.mBaseColor = to4f(pPBR->base_color_factor);
        mat.mRoughness = pPBR->roughness_factor;
        mat.mMetallic = pPBR->metallic_factor;

        mat.mBaseColorTexture = FALLBACK_BASECOLOR_INDEX;
        mat.mNormalTexture = FALLBACK_NORMAL_INDEX;
        mat.mMetallicRoughnessTexture = FALLBACK_MRS_INDEX;

        if(pPBR->base_color_texture.texture)
        {
            mat.mBaseColorTexture = cgltf_texture_index(pGltfData, pPBR->base_color_texture.texture) + FALLBACK_TEXTURE_COUNT;
            uint32 texInfoId = texInfoByPath[str(pPBR->base_color_texture.texture->image->uri)];
            pScene->mTexInfos[texInfoId].mFormat = FORMAT_RGBA8_SRGB;
        }
        if(pPBR->metallic_roughness_texture.texture)
        {
            mat.mMetallicRoughnessTexture = cgltf_texture_index(pGltfData, pPBR->metallic_roughness_texture.texture) + FALLBACK_TEXTURE_COUNT;
            uint32 texInfoId = texInfoByPath[str(pPBR->metallic_roughness_texture.texture->image->uri)];
            pScene->mTexInfos[texInfoId].mFormat = FORMAT_RGBA8_UNORM;
        }
        if(pMat->normal_texture.texture)
        {
            mat.mNormalTexture = cgltf_texture_index(pGltfData, pMat->normal_texture.texture) + FALLBACK_TEXTURE_COUNT;
            uint32 texInfoId = texInfoByPath[str(pMat->normal_texture.texture->image->uri)];
            pScene->mTexInfos[texInfoId].mFormat = FORMAT_RGBA8_UNORM;
        }
        if(pMat->alpha_mode != cgltf_alpha_mode_opaque)
        {
            ASSERT(pMat->alpha_mode == cgltf_alpha_mode_mask);  // TODO(caio): Support alpha blending
            mat.mAlphaCutoff = pMat->alpha_cutoff;
        }
        mat.mDoubleSided = (uint32)pMat->double_sided;

        pScene->mMaterials[pScene->mMaterialCount++] = mat;
    }

    // For every primitive within every mesh, read vertices and indices,
    // then append to appropriate buffers.
    // First pass: find vertex + index buffer sizes
    uint64 vertexCount = 0;
    uint64 indexCount = 0;
    for(cgltf_size m = 0; m < pGltfData->meshes_count; m++)
    {
        cgltf_mesh* pMesh = &pGltfData->meshes[m];
        for(cgltf_size p = 0; p < pMesh->primitives_count; p++)
        {
            cgltf_primitive* pPrimitive = &pMesh->primitives[p];
            ASSERT(pPrimitive->type = cgltf_primitive_type_triangles);

            cgltf_accessor* pAccPos     = NULL;
            for(cgltf_size a = 0; a < pPrimitive->attributes_count; a++)
            {
                cgltf_attribute* pAttr = &pPrimitive->attributes[a];
                if(pAttr->type == cgltf_attribute_type_position)
                    pAccPos = pAttr->data;
            }

            ASSERT(pAccPos);
            vertexCount += pAccPos->count;
            ASSERT(pPrimitive->indices);
            indexCount += pPrimitive->indices->count;
        }
    }

    pScene->vertexCount = vertexCount;
    pScene->indexCount = indexCount;
    pScene->pVertexData = (float*)arenaPushZero(&pScene->mArena, vertexCount * sizeof(float) * 12);
    pScene->pIndexData  = (uint16*)arenaPushZero(&pScene->mArena, indexCount * sizeof(uint16));

    // Second pass: copy to allocated buffers, create scene meshes and map them by cgltf_primitive
    void* pVertexOffset = pScene->pVertexData;
    void* pIndexOffset = pScene->pIndexData;
    HashMap<void*, uint32> sceneMeshByPrimitive = hashmap<void*, uint32>(&pScene->mTempArena, 2 << 12);
    for(cgltf_size m = 0; m < pGltfData->meshes_count; m++)
    {
        cgltf_mesh* pMesh = &pGltfData->meshes[m];
        for(cgltf_size p = 0; p < pMesh->primitives_count; p++)
        {
            cgltf_primitive* pPrimitive = &pMesh->primitives[p];
            ASSERT(pPrimitive->type = cgltf_primitive_type_triangles);

            // Vertex format is fixed: pos(3) normal (3) uv (2) tangent (4)
            cgltf_accessor* pAccPos     = NULL;
            cgltf_accessor* pAccNorm    = NULL;
            cgltf_accessor* pAccUV      = NULL;
            cgltf_accessor* pAccTan     = NULL;

            for(cgltf_size a = 0; a < pPrimitive->attributes_count; a++)
            {
                cgltf_attribute* pAttr = &pPrimitive->attributes[a];
                if(pAttr->type == cgltf_attribute_type_position)      pAccPos   = pAttr->data;
                else if(pAttr->type == cgltf_attribute_type_normal)   pAccNorm  = pAttr->data;
                else if(pAttr->type == cgltf_attribute_type_texcoord) pAccUV    = pAttr->data;
                else if(pAttr->type == cgltf_attribute_type_tangent)  pAccTan   = pAttr->data;
            }

            ASSERT(pAccPos);
            ASSERT(pAccPos->component_type == cgltf_component_type_r_32f);

            SceneMesh mesh = {};
            mesh.mVertexOffset = (int32)(PTR_DIFF(pVertexOffset, pScene->pVertexData) / (12 * sizeof(float)));

            for(cgltf_size v = 0; v < pAccPos->count; v++)
            {
                float vertex[12] =
                {
                    0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0
                };

                cgltf_accessor_read_float(pAccPos, v, &vertex[0], 3);
                if(pAccNorm)
                {
                    cgltf_accessor_read_float(pAccNorm, v, &vertex[3], 3);
                }
                if(pAccUV)
                {
                    cgltf_accessor_read_float(pAccUV, v, &vertex[6], 2);
                }
                if(pAccTan)
                {
                    cgltf_accessor_read_float(pAccTan, v, &vertex[8], 4);
                }

                memcpy(pVertexOffset, vertex, 12 * sizeof(float));
                pVertexOffset = PTR_OFFSET(pVertexOffset, 12 * sizeof(float));
            }

            cgltf_accessor* pAccIndices = pPrimitive->indices;
            ASSERT(pAccIndices->component_type == cgltf_component_type_r_16u);
            
            mesh.mIndexOffset = (uint32)(PTR_DIFF(pIndexOffset, pScene->pIndexData) / sizeof(uint16));
            mesh.mIndexCount = (uint32)(pAccIndices->count);

            for(cgltf_size i = 0; i < pAccIndices->count; i++)
            {
                uint16 idx = cgltf_accessor_read_index(pAccIndices, i);
                *((uint16*)pIndexOffset) = idx;
                pIndexOffset = PTR_OFFSET(pIndexOffset, sizeof(uint16));
            }

            uint32 midx = pScene->mMeshCount;
            pScene->mMeshes[midx] = mesh;
            sceneMeshByPrimitive.insert(pPrimitive, midx);
            pScene->mMeshCount++;
        }
    }

    // Third pass: create scene nodes, one for each glTF leaf node's meshes' primitives.
    for(cgltf_size n = 0; n < pGltfData->nodes_count; n++)
    {
        cgltf_node* pNode = &pGltfData->nodes[n];
        if(pNode->children_count) continue;     // Not a leaf node.
        if(!pNode->mesh) continue;

        for(cgltf_size p = 0; p < pNode->mesh->primitives_count; p++)
        {
            cgltf_primitive* pPrimitive = &pNode->mesh->primitives[p];
            uint32 meshIdx = sceneMeshByPrimitive[pPrimitive];

            uint32 materialIdx = MAX_UINT32;
            ASSERT(pPrimitive->material);
            materialIdx = (uint32)cgltf_material_index(pGltfData, pPrimitive->material);

            m4f xform;
            cgltf_node_transform_world(pNode, &xform.mData[0]);

            cgltf_accessor* pAccPos = NULL;
            for(cgltf_size a = 0; a < pPrimitive->attributes_count; a++)
            {
                cgltf_attribute* pAttr = &pPrimitive->attributes[a];
                if(pAttr->type == cgltf_attribute_type_position)
                {
                    pAccPos = pAttr->data;
                }
            }
            ASSERT(pAccPos && pAccPos->has_min && pAccPos->has_max);
            AABB aabb = { to3f(pAccPos->min), to3f(pAccPos->max) };
            aabb = transformAABB(aabb, xform);

            SceneNode sceneNode = { xform, to4f(aabb.min, 1), to4f(aabb.max, 1), meshIdx, materialIdx };
            pScene->mNodes[pScene->mNodeCount++] = sceneNode;
        }
    }

    // Reset scratch memory
    cgltf_free(pGltfData);
    ARENA_CHECKPOINT_RESET(&pScene->mTempArena, sceneModel);
}
