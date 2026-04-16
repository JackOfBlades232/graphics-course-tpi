#ifndef TERRAIN_MESH_H_INCLUDED
#define TERRAIN_MESH_H_INCLUDED

layout(binding = 2, set = 0) uniform sampler2D geomClipmap[CLIPMAP_LEVEL_COUNT];
layout(binding = 3, set = 0) uniform sampler2D normalClipmap[CLIPMAP_LEVEL_COUNT];
layout(binding = 4, set = 0) uniform sampler2D albedoClipmap[CLIPMAP_LEVEL_COUNT];
layout(binding = 5, set = 0) uniform sampler2D matdataClipmap[CLIPMAP_LEVEL_COUNT];

layout(binding = 7, set = 0) uniform terrain_source_t
{
  TerrainSourceData terrainSource;
};

const float OFFSET_FOR_NORMAL_EVAL = 0.01f;

uint get_level_info(vec2 w_rel_point)
{
  const float relOffs =
    max(abs(w_rel_point.x), abs(w_rel_point.y)) / CLIPMAP_EXTENT_STEP;
  const float levelExact = log2(relOffs);
  return min(uint(ceil(levelExact + 0.01f)), CLIPMAP_LEVEL_COUNT - 1);
}

void get_mip_info(
  vec2 w_rel_point, vec2 wrp_ddx, vec2 wrp_ddy, out uint base_level, out uint next_level, out float coeff)
{
  const uint minLevel = get_level_info(w_rel_point);

  vec2 dx = wrp_ddx * CLIPMAP_L0_TEXELS_PER_WORLD_UNIT;
  vec2 dy = wrp_ddy * CLIPMAP_L0_TEXELS_PER_WORLD_UNIT;
  float lod = log2(max(length(dx), length(dy)));
  float rawBase = floor(lod);
  
  base_level = uint(clamp(rawBase    , minLevel, CLIPMAP_LEVEL_COUNT - 1));
  next_level = uint(clamp(rawBase + 1, minLevel, CLIPMAP_LEVEL_COUNT - 1));
  coeff = fract(lod);
}

vec2 get_toroidal_uv(vec2 w_rel_point, uint level)
{
  const vec2 extent = float(1 << level) * vec2(CLIPMAP_EXTENT_STEP);
  const vec2 size = 2.f * extent;

  vec2 wRel = w_rel_point;
  if (level == CLIPMAP_LEVEL_COUNT - 1)
    wRel = clamp(wRel, -extent * 0.99f, extent * 0.99f);

  const vec2 uvToCenter = wRel / size;
  const vec2 uvFlat = uvToCenter + 0.5f;
  const vec2 uvClamped = clamp(fract(uvFlat), 0.0001f, 0.9999f);

  const vec2 toroidalScrolloff = fract(constants.toroidalUpdatePlayerWorldPos / size) - 0.5f;

  return fract(uvClamped + toroidalScrolloff);
}

float sample_geom_clipmap_level(vec2 w_rel_point, uint level)
{
  vec2 uv = get_toroidal_uv(w_rel_point, level);
  return textureLod(geomClipmap[level], uv, 0).x;
}

vec3 sample_normal_clipmap_level(vec2 w_rel_point, uint level)
{
  vec2 uv = get_toroidal_uv(w_rel_point, level);
  return textureLod(normalClipmap[level], uv, 0).xyz;
}

vec4 sample_albedo_clipmap_level(vec2 w_rel_point, uint level)
{
  vec2 uv = get_toroidal_uv(w_rel_point, level);
  return textureLod(albedoClipmap[level], uv, 0).xyzw;
}

vec4 sample_matdata_clipmap_level(vec2 w_rel_point, uint level)
{
  vec2 uv = get_toroidal_uv(w_rel_point, level);
  return textureLod(matdataClipmap[level], uv, 0);
}

float load_geom_clipmap_level(vec2 w_rel_point, uint level)
{
  ivec2 uv = ivec2(get_toroidal_uv(w_rel_point, level) * textureSize(geomClipmap[level], 0));
  return texelFetch(geomClipmap[level], uv, 0).x;
}

vec3 load_normal_clipmap_level(vec2 w_rel_point, uint level)
{
  ivec2 uv = ivec2(get_toroidal_uv(w_rel_point, level) * textureSize(normalClipmap[level], 0));
  return texelFetch(normalClipmap[level], uv, 0).xyz;
}

vec4 load_albedo_clipmap_level(vec2 w_rel_point, uint level)
{
  ivec2 uv = ivec2(get_toroidal_uv(w_rel_point, level) * textureSize(albedoClipmap[level], 0));
  return texelFetch(albedoClipmap[level], uv, 0);
}

vec4 load_matdata_clipmap_level(vec2 w_rel_point, uint level)
{
  ivec2 uv = ivec2(get_toroidal_uv(w_rel_point, level) * textureSize(matdataClipmap[level], 0));
  return texelFetch(matdataClipmap[level], uv, 0);
}

float load_geom_clipmap(vec2 w_rel_point)
{
  return load_geom_clipmap_level(w_rel_point, get_level_info(w_rel_point));
}

vec3 load_normal_clipmap(vec2 w_rel_point)
{
  return load_normal_clipmap_level(w_rel_point, get_level_info(w_rel_point));
}

vec4 load_albedo_clipmap(vec2 w_rel_point)
{
  return load_albedo_clipmap_level(w_rel_point, get_level_info(w_rel_point));
}

vec4 load_matdata_clipmap(vec2 w_rel_point)
{
  return load_matdata_clipmap_level(w_rel_point, get_level_info(w_rel_point));
}

float sample_geom_clipmap_exact(vec2 w_rel_point)
{
  return sample_geom_clipmap_level(w_rel_point, get_level_info(w_rel_point));
}

vec3 sample_normal_clipmap_exact(vec2 w_rel_point)
{
  return sample_normal_clipmap_level(w_rel_point, get_level_info(w_rel_point));
}

vec4 sample_albedo_clipmap_exact(vec2 w_rel_point)
{
  return sample_albedo_clipmap_level(w_rel_point, get_level_info(w_rel_point));
}

vec4 sample_matdata_clipmap_exact(vec2 w_rel_point)
{
  return sample_matdata_clipmap_level(w_rel_point, get_level_info(w_rel_point));
}

float sample_geom_clipmap(vec2 w_rel_point, vec2 wrp_ddx, vec2 wrp_ddy)
{
  // @TODO: Factor player z in

  uint baseLevel;
  uint nextLevel;
  float coeff;
  get_mip_info(w_rel_point, wrp_ddx, wrp_ddy, baseLevel, nextLevel, coeff);

  float val = sample_geom_clipmap_level(w_rel_point, baseLevel);
  if (nextLevel != baseLevel)
  {
    float nextSample = sample_geom_clipmap_level(w_rel_point, nextLevel);
    val *= 1.f - coeff;
    val += coeff * nextSample;
  }

  return val;
}

vec3 sample_normal_clipmap(vec2 w_rel_point, vec2 wrp_ddx, vec2 wrp_ddy)
{
  uint baseLevel;
  uint nextLevel;
  float coeff;
  get_mip_info(w_rel_point, wrp_ddx, wrp_ddy, baseLevel, nextLevel, coeff);

  vec3 val = sample_normal_clipmap_level(w_rel_point, baseLevel);
  if (nextLevel != baseLevel)
  {
    vec3 nextSample = sample_normal_clipmap_level(w_rel_point, nextLevel);
    val *= 1.f - coeff;
    val += coeff * nextSample;
  }

  return normalize(val);
}

vec4 sample_albedo_clipmap(vec2 w_rel_point, vec2 wrp_ddx, vec2 wrp_ddy)
{
  uint baseLevel;
  uint nextLevel;
  float coeff;
  get_mip_info(w_rel_point, wrp_ddx, wrp_ddy, baseLevel, nextLevel, coeff);

  vec4 val = sample_albedo_clipmap_level(w_rel_point, baseLevel);
  if (nextLevel != baseLevel)
  {
    vec4 nextSample = sample_albedo_clipmap_level(w_rel_point, nextLevel);
    val *= 1.f - coeff;
    val += coeff * nextSample;
  }

  return val;
}

vec4 sample_matdata_clipmap(vec2 w_rel_point, vec2 wrp_ddx, vec2 wrp_ddy)
{
  uint baseLevel;
  uint nextLevel;
  float coeff;
  get_mip_info(w_rel_point, wrp_ddx, wrp_ddy, baseLevel, nextLevel, coeff);

  vec4 val = sample_matdata_clipmap_level(w_rel_point, baseLevel);
  if (nextLevel != baseLevel)
  {
    vec4 nextSample = sample_matdata_clipmap_level(w_rel_point, nextLevel);
    val *= 1.f - coeff;
    val += coeff * nextSample;
  }

  return val;
}

#endif // TERRAIN_MESH_H_INCLUDED
