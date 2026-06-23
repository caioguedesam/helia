// Scene defines
#define SCENE_MAX_TEXTURES 1024
#define FALLBACK_BASECOLOR_INDEX 0
#define FALLBACK_NORMAL_INDEX 1
#define FALLBACK_MRS_INDEX 2
#define FALLBACK_TEXTURE_COUNT 3

#define SCENE_MAX_NODES 1024
#define SCENE_MAX_MESHES SCENE_MAX_NODES
#define SCENE_MAX_MATERIALS SCENE_MAX_NODES

// Draw buffer defines
#define MAX_DRAW_BUFFERS 16
#define MAX_DRAWS 512

#define DB_SHADOW_0 0
#define DB_SHADOW_1 1
#define DB_SHADOW_2 2
#define DB_GBUFFER_OPAQUE 3
#define DB_GBUFFER_OPAQUE_DOUBLE 4
#define DB_COUNT 5

// Renderer defines
#define CONCURRENT_FRAMES 2
#define MAX_CASCADES 3
#define MAX_DEBUG_VERTS 1000000
#define HIZ_MAX 10
#define SHADOW_MAP_SIZE 2048

// App defines
#define APP_WIDTH 800
#define APP_HEIGHT 600
