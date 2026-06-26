#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "draw.h"
#include "water.h"
#include "constants.h"


layout(quads, fractional_even_spacing, ccw) in;

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
  vec2 texCoord;
} teOut;

// @TODO: pull out to water_mesh.glsl.inc
layout(binding = 6, set = 0) uniform water_source_t
{
  WaterSourceData source;
};

void main(void)
{
  const vec2 baseXZ = gl_in[0].gl_Position.xz;
  const vec2 extentXZ = gl_in[2].gl_Position.xz - baseXZ;

  const vec2 pointXZ = baseXZ + gl_TessCoord.xy * extentXZ;

  // @TEST
  teOut.wPos = vec3(pointXZ.x, source.waterLevel, pointXZ.y);
  teOut.texCoord = vec2(0.f);

  gl_Position = calc_adjusted_viewproj_mat(viewParams, viewData) * vec4(teOut.wPos, 1.f);
}
