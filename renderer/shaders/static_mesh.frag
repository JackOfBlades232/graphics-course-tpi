#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : enable

#include "constants.h"

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

layout(location = 0) in VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec4 wTangent;
  vec2 texCoord;
  flat uint matId;
} surf;

#include "material_mesh.glsl.inc"
#include "motion_vectors.glsl.inc"
#include "temporal_helpers.frag.inc"

void main(void)
{
  const uint matId = uint(surf.matId);

  vec4 prevNdc = calc_prev_adjusted_viewproj_mat(viewParams, viewData) * vec4(surf.wPos, 1.f);
  vec2 prevNdcXy = prevNdc.xy / prevNdc.w;

  vec2 tc = unjitter_surface_texcoord(surf.texCoord, viewParams);

  get_pixel_gbuf_info(
    matId, surf.wNorm, surf.wTangent, tc,
    out_fragAlbedo, out_fragMaterial, out_fragNormal, out_fragTransmission);
  get_static_pixel_motion_vector(
    gl_FragCoord.xy,
    constants.mainTargetResolution,
    prevNdcXy,
    get_subpixel_uv_jitter(viewParams),
    viewParams.prevSubpixelUvJitter,
    out_motionVector);
}
