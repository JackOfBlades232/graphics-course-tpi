#ifndef TERRAIN_H_INCLUDED
#define TERRAIN_H_INCLUDED

#include "materials.h"
#include "cpp_glsl_compat.h"

#define TERRAIN_MAX_DETAILS 8

#define TERRAIN_DETAIL_USE_MASK_FLAG (1)
#define TERRAIN_DETAIL_USE_RH_RANGE_FLAG (1 << 1)

struct TerrainDetailRule
{
  shader_vec2 uvScale;
  shader_vec2 heightRange;
  shader_uint splattingCompId;
  shader_uint splattingCompMask;
  shader_uint matId;
  shader_uint flags;
};

struct TerrainSourceData
{
  TexSmpIdPair heightmapTexSmp;
  TexSmpIdPair splattingMaskTexSmp;

  shader_uint noiseSeed;
  shader_uint detailCount;

  shader_vec3 rangeMin;
  shader_uint pad1_;

  shader_vec3 rangeMax;
  shader_uint pad2_;

  TerrainDetailRule details[TERRAIN_MAX_DETAILS];
};

#define CLIPMAP_LEVEL_COUNT 8

// @TODO: tweakable
#define CLIPMAP_RESOLUTION 2048
#define CLIPMAP_HALF_RESOLUTION (CLIPMAP_RESOLUTION / 2)
#define CLIPMAP_EXTENT_STEP 5.f
#define CLIPMAP_UPDATE_MIN_DPOS 0.1f

// @TODO: tweakable?
#define TERRAIN_CHUNKS_LEVEL_DIM 4
#define TERRAIN_FIRST_LEVEL_CHUNKS (TERRAIN_CHUNKS_LEVEL_DIM * TERRAIN_CHUNKS_LEVEL_DIM)
#define TERRAIN_OTHER_LEVELS_CHUNKS (TERRAIN_FIRST_LEVEL_CHUNKS - (TERRAIN_FIRST_LEVEL_CHUNKS / 4))

// @TODO: not only tweakable, but dependent on data
#define TERRAIN_NOISE_REL_HEIGHT_AMPLITUDE 0.002f
#define TERRAIN_NOISE_PERIOD 0.25f
#define TERRAIN_CHUNK_TESSELLATION_FACTOR 64
#define TERRAIN_NORMAL_SAMPLING_PIXEL_OFFS 0.01f

#define TERRAIN_DETAIL_LEVEL_FALLOFF_TO_SCALE_X 0.5f

// @TODO: sort out maths around toroidal, now it's kinda hacky

shader_inline shader_ivec2 calculate_toroidal_dims(shader_vec2 w_offset, shader_uint level)
{
  const float worldSize = 2.f * CLIPMAP_EXTENT_STEP * float(1 << level);
  const float imageSize = float(CLIPMAP_RESOLUTION * (1 << level));
  const shader_vec2 levelUvOffset = shader_clamp(w_offset / worldSize, -1.f, 1.f);
  shader_ivec2 pixelOffsetRaw = shader_ivec2(round_from_zero(levelUvOffset * imageSize));
  pixelOffsetRaw += shader_sign(pixelOffsetRaw);
  return -shader_clamp(
    pixelOffsetRaw,
    -shader_ivec2(CLIPMAP_RESOLUTION, CLIPMAP_RESOLUTION),
    shader_ivec2(CLIPMAP_RESOLUTION, CLIPMAP_RESOLUTION));
}

shader_inline shader_uint calculate_thread_count_for_clipmap_update(shader_ivec2 update_dims)
{
  return shader_abs(update_dims.x) * CLIPMAP_RESOLUTION +
    shader_abs(update_dims.y) * CLIPMAP_RESOLUTION - shader_abs(update_dims.x * update_dims.y);
}

#endif // TERRAIN_H_INCLUDED
