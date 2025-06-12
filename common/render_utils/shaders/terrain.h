#ifndef TERRAIN_H_INCLUDED
#define TERRAIN_H_INCLUDED

#include "materials.h"
#include "cpp_glsl_compat.h"

struct TerrainSourceData
{
  shader_mat4 transform;

  TexSmpIdPair heightmapTexSmp;
  TexSmpIdPair diffuseTexSmp;
  TexSmpIdPair errosionTexSmp;

  shader_uint errosionNoiseSeed;

  shader_vec2 heightmapRange;

  shader_uint pad1_, pad2_;
};

#endif // TERRAIN_H_INCLUDED
