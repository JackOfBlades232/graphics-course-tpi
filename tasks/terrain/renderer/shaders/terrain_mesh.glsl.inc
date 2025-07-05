#ifndef TERRAIN_MESH_H_INCLUDED
#define TERRAIN_MESH_H_INCLUDED

#endif // TERRAIN_MESH_H_INCLUDED

layout(binding = 2, set = 0) uniform sampler2D geomClipmap[CLIPMAP_LEVEL_COUNT];
layout(binding = 3, set = 0) uniform sampler2D normalClipmap[CLIPMAP_LEVEL_COUNT];
layout(binding = 4, set = 0) uniform sampler2D albedoClipmap[CLIPMAP_LEVEL_COUNT];

layout(binding = 7, set = 0) uniform terrain_source_t
{
  TerrainSourceData terrainSource;
};

const float OFFSET_FOR_NORMAL_EVAL = 0.01f;

void get_mip_info(
  vec2 w_rel_point, out uint base_level, out uint next_level, out float coeff)
{
  const float relOffs =
    max(abs(w_rel_point.x), abs(w_rel_point.y)) / CLIPMAP_EXTENT_STEP;
  const float levelExact = max(log2(relOffs + 1.f), 0.f);
  base_level = uint(floor(levelExact));
  next_level = base_level + 1;
  coeff = levelExact - float(base_level);
}

vec2 get_toroidal_uv(vec2 w_rel_point, uint level)
{
  const vec2 extent = float(1 << level) * vec2(CLIPMAP_EXTENT_STEP);
  const vec2 size = 2.f * extent;
  const vec2 relToCorner = w_rel_point + extent;
  const vec2 uvFlat = relToCorner / size;

  const vec2 toroidalScrolloff =
    fract(constants.lastToroidalUpdatePlayerWorldPos.xz / size) - 0.5f;

  return fract(uvFlat + toroidalScrolloff + 1.f);
}

float sample_geom_clipmap_level(vec2 w_rel_point, uint level)
{
  return textureLod(geomClipmap[level], get_toroidal_uv(w_rel_point, level), 0).x;
}

vec3 sample_normal_clipmap_level(vec2 w_rel_point, uint level)
{
  return textureLod(normalClipmap[level], get_toroidal_uv(w_rel_point, level), 0).xyz;
}

// @TODO: pull out
float sample_geom_clipmap(vec2 w_rel_point)
{
  // @TODO: calculate lod from player pos? Doesn't seem better atm, and will
  // have to deal somehow with edges if we are out of the inner level

  // @TODO: proper mip coeff/level choice -- ddx ddy (check how it works)
  // @TODO: Factor player z in

  uint baseLevel;
  uint nextLevel;
  float coeff;
  get_mip_info(w_rel_point, baseLevel, nextLevel, coeff);

  float val = sample_geom_clipmap_level(w_rel_point, baseLevel);
  /*
  if (nextLevel < CLIPMAP_LEVEL_COUNT)
  {
    float nextSample = sample_geom_clipmap_level(w_rel_point, nextLevel);
    val *= 1.f - coeff;
    val += coeff * nextSample;
  }
  */

  return val;
}

vec3 sample_normal_clipmap(vec2 w_rel_point)
{
  // @TODO: calculate lod from player pos? Doesn't seem better atm, and will
  // have to deal somehow with edges if we are out of the inner level

  // @TODO: proper mip coeff/level choice -- ddx ddy (check how it works)
  // @TODO: Factor player z in

  uint baseLevel;
  uint nextLevel;
  float coeff;
  get_mip_info(w_rel_point, baseLevel, nextLevel, coeff);

  vec3 val = sample_normal_clipmap_level(w_rel_point, baseLevel);
  /*
  if (nextLevel < CLIPMAP_LEVEL_COUNT)
  {
    vec3 nextSample = sample_normal_clipmap_level(w_rel_point, nextLevel);
    val *= 1.f - coeff;
    val += coeff * nextSample;
  }
  */

  return normalize(val);
}
