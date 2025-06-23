#ifndef TERRAIN_H_INCLUDED
#define TERRAIN_H_INCLUDED

#include "materials.h"
#include "cpp_glsl_compat.h"

struct TerrainSourceData
{
  TexSmpIdPair heightmapTexSmp;

  shader_uint noiseSeed;

  shader_uint pad1_, pad2_;

  shader_vec3 rangeMin;
  shader_uint pad3_;

  shader_vec3 rangeMax;
  shader_uint pad4_;
};

#define CLIPMAP_LEVEL_COUNT 8

// @TODO: tweakable
#define CLIPMAP_RESOLUTION 1024
#define CLIPMAP_HALF_RESOLUTION (CLIPMAP_RESOLUTION / 2)
#define CLIPMAP_EXTENT_STEP 10.f
#define CLIPMAP_UPDATE_MIN_DPOS 0.1f//1.f

shader_inline shader_ivec2 calculate_toroidal_dims(shader_vec2 w_offset, shader_uint level)
{
  const float worldSize = 2.f * CLIPMAP_EXTENT_STEP * float(1 << level);
  const float imageSize = float(CLIPMAP_RESOLUTION * (1 << level));
  // @TODO: fract
  const shader_vec2 levelUvOffset = shader_clamp(w_offset / worldSize, -1.f, 1.f);
  const shader_ivec2 pixelOffsetRaw = shader_ivec2(
    shader_int(levelUvOffset.x * imageSize), shader_int(levelUvOffset.y * imageSize));
  return shader_clamp(
    pixelOffsetRaw,
    -shader_ivec2(CLIPMAP_RESOLUTION, CLIPMAP_RESOLUTION),
    shader_ivec2(CLIPMAP_RESOLUTION, CLIPMAP_RESOLUTION));
}

shader_inline shader_uint calculate_thread_count_for_clipmap_update(shader_ivec2 update_dims)
{
  return
    shader_abs(update_dims.x) * CLIPMAP_RESOLUTION +
    shader_abs(update_dims.y) * CLIPMAP_RESOLUTION -
    shader_abs(update_dims.x * update_dims.y);
}

#endif // TERRAIN_H_INCLUDED
