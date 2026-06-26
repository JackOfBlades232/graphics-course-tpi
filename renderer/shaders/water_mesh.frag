#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : enable

#include "materials.h"
#include "water.h"
#include "constants.h"
#include "quantization.h"


// @TODO: find an appropriate way to unify with static_mesh

layout(location = 0) out vec4 out_fragAlbedo;
layout(location = 1) out vec3 out_fragMaterial;
layout(location = 2) out vec3 out_fragNormal;
layout(location = 3) out vec4 out_fragTransmission;
layout(location = 4) out vec2 out_motionVector;

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

layout(location = 0) in TE_OUT
{
  vec3 wPos;
  vec3 wNormal;
  float foamfactor;
} surf;

#include "motion_vectors.glsl.inc"

void main(void)
{
  vec4 surfaceColor;
  vec3 materialData;
  vec3 normal;

  // @TEST
  vec4 waterColor = vec4(0.f, 0.4f, 1.f, 1.f);
  vec3 waterMatdata = vec3(float(MATERIAL_PBR), 0.f, 0.2f);

  vec4 foamColor = vec4(1.f, 1.f, 1.f, 1.f);
  vec3 foamMatdata = vec3(float(MATERIAL_PBR), 0.f, 0.9f);

  surfaceColor = surf.foamfactor < 0.f ? foamColor : waterColor;
  materialData = surf.foamfactor < 0.f ? foamMatdata : waterMatdata;
  normal = surf.wNormal;

  out_fragAlbedo = surfaceColor;
  out_fragMaterial = materialData;
  out_fragNormal = normal;
  out_fragTransmission = vec4(vec3(1.f), 0.f);

  vec4 prevNdc = calc_prev_adjusted_viewproj_mat(viewParams, viewData) * vec4(surf.wPos, 1.f);
  vec2 prevNdcXy = prevNdc.xy / prevNdc.w;

  get_static_pixel_motion_vector(
    gl_FragCoord.xy,
    constants.mainTargetResolution,
    prevNdcXy,
    get_subpixel_uv_jitter(viewParams),
    viewParams.prevSubpixelUvJitter,
    out_motionVector);
}

