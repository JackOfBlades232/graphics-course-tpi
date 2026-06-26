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
  vec2 texCoord;
} surf;

#include "motion_vectors.glsl.inc"
#include "temporal_helpers.frag.inc"

void main(void)
{
  vec4 surfaceColor;
  vec3 materialData;
  vec3 normal;

  vec2 tc = unjitter_surface_texcoord(surf.texCoord, viewParams);
  vec2 tcDdx = dFdx(tc);
  vec2 tcDdy = dFdy(tc);

  // @TEST
  surfaceColor = vec4(0.f, 0.4f, 1.f, 1.f);
  materialData = vec3(
    float(MATERIAL_DIFFUSE),
    uintBitsToFloat(quantize4fcol(vec4(0.f, 1.f, 1.f, 0.f))),
    0.f);
  normal = vec3(0.f, 1.f, 0.f);

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

