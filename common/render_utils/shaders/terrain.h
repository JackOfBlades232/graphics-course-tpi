#ifndef TERRAIN_H_INCLUDED
#define TERRAIN_H_INCLUDED

#include "materials.h"
#include "cpp_glsl_compat.h"

struct TerrainSourceData
{
  TexSmpIdPair heightmapTexSmp;

  shader_uint pad1_, pad2_, pad3_;

  shader_vec3 rangeMin;
  shader_uint pad4_;

  shader_vec3 rangeMax;
  shader_uint pad5_;
};

#define CLIPMAP_LEVEL_COUNT 8

// @TODO: tweakable
#define CLIPMAP_RESOLUTION 1024
#define CLIPMAP_EXTENT_STEP 10.f
#define CLIPMAP_UPDATE_MIN_DPOS 1.f

#endif // TERRAIN_H_INCLUDED
