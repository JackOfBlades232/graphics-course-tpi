#ifndef TERRAIN_H_INCLUDED
#define TERRAIN_H_INCLUDED

#include "materials.h"
#include "cpp_glsl_compat.h"

struct TerrainSourceData
{
  shader_mat4 transform;
  shader_mat4 inverseTransform;

  TexSmpIdPair heightmapTexSmp;
  TexSmpIdPair diffuseTexSmp;
  TexSmpIdPair errosionTexSmp;

  shader_uint errosionNoiseSeed;

  shader_vec3 rangeMin;
  shader_uint pad1_;

  shader_vec3 rangeMax;
  shader_uint pad2_;
};

#define CLIPMAP_LEVEL_COUNT 8

// @TODO: tweakable
#define CLIPMAP_RESOLUTION 1024
#define CLIPMAP_EXTENT_STEP 10.f

#endif // TERRAIN_H_INCLUDED
