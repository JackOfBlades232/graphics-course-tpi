#ifndef TERRAIN_H_INCLUDED
#define TERRAIN_H_INCLUDED

#include "materials.h"
#include "cpp_glsl_compat.h"

struct TerrainSourceData
{
  TexSmpIdPair heightmapTexSmp;

  shader_uint noiseSeed;

  shader_uint pad2_, pad3_;

  shader_vec3 rangeMin;
  shader_uint pad3_;

  shader_vec3 rangeMax;
  shader_uint pad4_;
};

#define CLIPMAP_LEVEL_COUNT 8

// @TODO: tweakable
#define CLIPMAP_RESOLUTION 1024
#define CLIPMAP_EXTENT_STEP 10.f
#define CLIPMAP_UPDATE_MIN_DPOS 1.f

shader_inline shader_ivec2 calculate_toroidal_dims(shader_vec2 w_offset, shader_uint level)
{
  const float worldExtent = 2.f * CLIPMAP_EXTENT_STEP * float(1 << level);
  const float imageExtent = float(CLIPMAP_RESOLUTION * (1 << level));
  // @TODO: fract
  const shader_vec2 levelUvOffset = w_offset / worldExtent;
  return shader_ivec2(
    shader_int(levelUvOffset.x * imageExtent), shader_int(levelUvOffset.y * imageExtent));
}

shader_inline shader_uint calculate_wg_size_for_clipmap_update(shader_ivec2 update_dims)
{
  return update_dims.x * CLIPMAP_RESOLUTION + update_dims.y * CLIPMAP_RESOLUTION -
    update_dims.x * update_dims.y;
}

#endif // TERRAIN_H_INCLUDED
