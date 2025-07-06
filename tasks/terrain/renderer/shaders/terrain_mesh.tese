#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "draw.h"
#include "terrain.h"
#include "constants.h"


layout(quads, fractional_even_spacing, ccw) in;

// @TEST
layout(location = 0) patch in TC_OUT
{
  uint isTop;
  uint isBottom;
  uint isLeft;
  uint isRight;
} surf;

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

#include "terrain_mesh.glsl.inc"

layout(location = 0) out TE_OUT
{
  vec3 wPos;
  vec2 texCoord;

  // @TEST
  vec4 vcol;
} teOut;

void main(void)
{
  const vec2 baseXZ = gl_in[0].gl_Position.xz;
  const vec2 extentXZ = gl_in[2].gl_Position.xz - baseXZ;

  const vec2 pointXZ = baseXZ + gl_TessCoord.xy * extentXZ;

  const vec2 wOffsetFromClipmapCenter =
    pointXZ - constants.toroidalUpdatePlayerWorldPos.xz;

  teOut.wPos = vec3(pointXZ.x, sample_geom_clipmap(wOffsetFromClipmapCenter), pointXZ.y);
  teOut.texCoord = wOffsetFromClipmapCenter; // Special for clipmap sampling

  // @TEST
  vec3 col = vec3(0.5f, 0.9f, 0.4f) * 0.5f;
  /*
  float cnt = 1.f;
  if (surf.isTop == 1)
  {
    col += vec3(1.f, 0.f, 0.f);
    cnt += 1.f;
  }
  if (surf.isBottom == 1)
  {
    col += vec3(1.f, 1.f, 0.f);
    cnt += 1.f;
  }
  if (surf.isLeft == 1)
  {
    col += vec3(0.f, 0.f, 1.f);
    cnt += 1.f;
  }
  if (surf.isRight == 1)
  {
    col += vec3(0.f, 1.f, 1.f);
    cnt += 1.f;
  }
  col /= cnt;
  */

  teOut.vcol = vec4(col, 1.f);

  gl_Position = constants.mProjView * vec4(teOut.wPos, 1.0);
}
