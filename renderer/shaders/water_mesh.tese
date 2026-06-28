#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "draw.h"
#include "water.h"
#include "constants.h"


layout(quads, fractional_even_spacing, ccw) in;

layout(binding = 2, set = 0) uniform sampler2D displacement[WATER_CASCADE_COUNT];

layout(binding = 6, set = 0) uniform water_source_t
{
  WaterSourceData source;
};
layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};
layout(binding = 9, set = 0) uniform view_params_t
{
  ViewParams viewParams;
};
layout(binding = 10, set = 0) readonly buffer view_data_t
{
  ViewData viewData;
};

layout(location = 0) out TE_OUT
{
  vec3 wPos;
  vec2 wInitPlanarPos;
} teOut;

vec3 sample_cascade(vec2 world_planar_pos, uint cid)
{
  float l = 0.f;
  switch (cid)
  {
  case 0:  l = source.l0; break;
  case 1:  l = source.l1; break;
  case 2:  l = source.l2; break;
  default:                break;
  }

  vec2 uv = world_planar_pos / l;
  return textureLod(displacement[cid], uv, 0.f).xyz;
}

void main(void)
{
  const vec2 baseXZ = gl_in[0].gl_Position.xz;
  const vec2 extentXZ = gl_in[2].gl_Position.xz - baseXZ;

  const vec2 pointXZ = baseXZ + gl_TessCoord.xy * extentXZ;

  vec3 disp = vec3(0.f);

  // @TEST
  for (uint cid = 0; cid < WATER_CASCADE_COUNT; ++cid)
    disp += sample_cascade(pointXZ, cid);

  teOut.wPos = vec3(pointXZ.x, source.waterLevel, pointXZ.y) + disp;
  teOut.wInitPlanarPos = pointXZ;

  gl_Position = calc_adjusted_viewproj_mat(viewParams, viewData) * vec4(teOut.wPos, 1.f);
}
