#define PI 3.1415926535
#define EPSILON 0.000001
#define dot_c(A, B) max(dot((A), (B)), 0.0)

#define CONCAT_(A, B) A##B
#define CONCAT(A, B) CONCAT_(A, B)

#define STRUCT_PADDING_UINT(NAME, AMOUNT) uint padding_##NAME[AMOUNT]
#define STRUCT_PADDING_VEC4(NAME, AMOUNT) vec4 padding_##NAME[AMOUNT]

#define DEFINE_UNIFORM_BLOCK(SET, BINDING) layout(std140, set = SET, binding = BINDING) uniform CONCAT(UNIFORM_BLOCK_, __LINE__)
#define DEFINE_STORAGE_BLOCK(SET, BINDING) layout(std430, set = SET, binding = BINDING) buffer CONCAT(UNIFORM_BLOCK_, __LINE__)
#define DEFINE_SAMPLER2D(SET, BINDING) layout(set = SET, binding = BINDING) uniform sampler2D
#define DEFINE_SAMPLER3D(SET, BINDING) layout(set = SET, binding = BINDING) uniform sampler3D

// TODO(caio): Macros for push constants?
// TODO(caio): Macros for resource arrays?
