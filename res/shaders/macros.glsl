#define PI 3.1415926535
#define EPSILON 0.000001
#define dot_c(A, B) max(dot((A), (B)), 0.0)
#define saturate(X) clamp(X, 0.0, 1.0)
#define FLT_MAX 3.402823466e+38F

#define CONCAT_(A, B) A##B
#define CONCAT(A, B) CONCAT_(A, B)

#define STRUCT_PADDING_UINT(NAME, AMOUNT) uint padding_##NAME[AMOUNT]
#define STRUCT_PADDING_VEC4(NAME, AMOUNT) vec4 padding_##NAME[AMOUNT]

#define VS_IN(N) layout(location = N) in
#define VS_OUT(N) layout(location = N) out
#define VS_OUT_NOINTERP(N) layout(location = N) flat out
#define PS_IN(N) layout(location = N) in
#define PS_IN_NOINTERP(N) layout(location = N) flat in
#define PS_OUT(N) layout(location = N) out

#define DEFINE_UNIFORM_BLOCK(SET, BINDING)  layout(std140, set = SET, binding = BINDING) uniform CONCAT(UNIFORM_BLOCK_, __LINE__)
#define DEFINE_STORAGE_BLOCK(SET, BINDING)  layout(std430, set = SET, binding = BINDING) buffer CONCAT(STORAGE_BLOCK_, __LINE__)
#define DEFINE_SAMPLER2D(SET, BINDING)      layout(set = SET, binding = BINDING) uniform sampler2D
#define DEFINE_SAMPLER3D(SET, BINDING)      layout(set = SET, binding = BINDING) uniform sampler3D
#define DEFINE_TEXTURE2D(SET, BINDING)      layout(set = SET, binding = BINDING) uniform texture2D
#define DEFINE_SAMPLER(SET, BINDING)        layout(set = SET, binding = BINDING) uniform sampler

#define DEFINE_IMAGE2D(SET, BINDING, FORMAT)      layout(FORMAT, set = SET, binding = BINDING) uniform image2D

#define DEFINE_CONSTANT_BLOCK layout(push_constant) uniform CONCAT(PUSH_CONSTANT_BLOCK_, __LINE__)
