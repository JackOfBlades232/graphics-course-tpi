#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "draw.h"
#include "terrain.h"
#include "constants.h"


layout(quads, equal_spacing, ccw) in;

layout(binding = 2, set = 0) uniform sampler2D geomClipmap[CLIPMAP_LEVEL_COUNT];

layout(binding = 7, set = 0) uniform terrain_source_t
{
  TerrainSourceData terrainSource;
};
layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

layout(location = 0) out TE_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec4 wTangent;
  vec2 texCoord;
} teOut;

const float OFFSET_FOR_NORMAL_EVAL = 0.1f;

// @TODO: pull out
vec4 sample_clipmap(vec2 w_rel_point)
{
  // @TODO: calculate lod from player pos? Doesn't seem better atm, and will
  // have to deal somehow with edges if we are out of the inner level

  // @TODO: Sample multiple levels and mix + sample higher levels when
  // z diff is big

  const uint level = clamp(
    uint(floor(log2(max(w_rel_point.x, w_rel_point.y) / CLIPMAP_EXTENT_STEP))),
    0, CLIPMAP_LEVEL_COUNT - 1);

  const vec2 extent = float(1 << level) * vec2(CLIPMAP_EXTENT_STEP);
  const vec2 size = 2.f * extent;
  const vec2 relToCorner = w_rel_point + extent;
  const vec2 uvFlat = relToCorner / size;

  const vec2 toroidalScrolloff =
    fract(constants.lastToroidalUpdatePlayerWorldPos.xz / size) - 0.5f;

  const vec2 toroidalUv = fract(uvFlat + toroidalScrolloff);

  return textureLod(geomClipmap[level], toroidalUv, 0);
}

float get_abs_height(vec2 w_rel_point)
{
  const float relH = sample_clipmap(w_rel_point).x;
  const float absH = terrainSource.rangeMin.y +
    relH * (terrainSource.rangeMax.y - terrainSource.rangeMin.y);
  return absH;
}

vec3 evaluate_normal(vec2 w_rel_point)
{
  const vec2 xl2 = w_rel_point - vec2(OFFSET_FOR_NORMAL_EVAL, 0.f);
  const vec2 xg2 = w_rel_point + vec2(OFFSET_FOR_NORMAL_EVAL, 0.f);
  const vec2 zl2 = w_rel_point - vec2(0.f, OFFSET_FOR_NORMAL_EVAL);
  const vec2 zg2 = w_rel_point + vec2(0.f, OFFSET_FOR_NORMAL_EVAL);

  const vec3 xl = vec3(xl2.x, get_abs_height(xl2), xl2.y);
  const vec3 xg = vec3(xg2.x, get_abs_height(xg2), xg2.y);
  const vec3 zl = vec3(zl2.x, get_abs_height(zl2), zl2.y);
  const vec3 zg = vec3(zg2.x, get_abs_height(zg2), zg2.y);

  return normalize(cross(xg - xl, zg - zl));
}

void main(void)
{
  const vec2 baseXZ = gl_in[0].gl_Position.xz;
  const vec2 extentXZ = gl_in[2].gl_Position.xz - baseXZ;

  const vec2 pointXZ = baseXZ + gl_TessCoord.xy * extentXZ;

  const vec2 wOffsetFromClipmapCenter =
    pointXZ - constants.lastToroidalUpdatePlayerWorldPos.xz;

  teOut.wPos = vec3(pointXZ.x, get_abs_height(wOffsetFromClipmapCenter), pointXZ.y);
  teOut.wNorm = evaluate_normal(wOffsetFromClipmapCenter);

  teOut.wTangent = vec4(0.f); // @TODO: gen
  teOut.texCoord = wOffsetFromClipmapCenter; // Special for clipmap sampling

  gl_Position = constants.mProjView * vec4(teOut.wPos, 1.0);
}
