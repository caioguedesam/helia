#version 460 core
#include "common.glsl"

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

shared float samples[8][8][3];

void GenerateMip(int i, ivec2 gID, out ivec2 sampleCoord)
{
    int prevMip = i - 1;

    float z0 = samples[sampleCoord.x][sampleCoord.y][prevMip];
    float z1 = samples[sampleCoord.x + 1][sampleCoord.y][prevMip];
    float z2 = samples[sampleCoord.x][sampleCoord.y + 1][prevMip];
    float z3 = samples[sampleCoord.x + 1][sampleCoord.y + 1][prevMip];
    
    float z = max(z0, max(z1, max(z2, z3)));

    sampleCoord = sampleCoord / 2;

    int denom = int(pow(2, i));
    ivec2 mipCoord = ivec2(gID.x / denom, gID.y / denom);
    imageStore(hiz[i], mipCoord, vec4(z, 0, 0, 0));

    if(i < 3)
    {
        samples[sampleCoord.x][sampleCoord.y][i] = z;
    }
}

void main()
{
    // Read 8x8 block, one texel per thread
    ivec2 gID = ivec2(gl_GlobalInvocationID.xy);
    float z = imageLoad(hiz[0], gID).r;

    ivec2 tID = ivec2(gl_LocalInvocationID.xy);
    samples[tID.x][tID.y][0] = z;

    barrier();

    // Fill block for mip 1 (4x4)
    ivec2 sampleCoord = tID;
    if(tID.x % 2 == 0 && tID.y % 2 == 0)
        GenerateMip(1, gID, sampleCoord); 
    barrier();

    // Fill block for mip 2 (2x2)
    if(tID.x % 4 == 0 && tID.y % 4 == 0)
        GenerateMip(2, gID, sampleCoord); 
    barrier();

    // Fill block for mip 3 (1x1)
    if(tID.x % 8 == 0 && tID.y % 8 == 0)
        GenerateMip(3, gID, sampleCoord); 

}
