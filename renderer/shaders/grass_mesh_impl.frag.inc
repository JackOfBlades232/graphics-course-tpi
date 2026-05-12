#ifndef GRASS_MESH_IMPL_FRAG
#define GRASS_MESH_IMPL_FRAG

#include "geometry.h"
#include "constants.h"

#if GBUF_PASS
layout(location = 0) out vec4 out_fragAlbedo;
layout(location = 1) out vec3 out_fragMaterial;
layout(location = 2) out vec3 out_fragNormal;
layout(location = 3) out vec4 out_fragTransmission;
layout(location = 4) out vec2 out_motionVector;
#endif

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
#if GBUF_PASS
  vec3 wPos;
  vec3 wPrevPos;
#endif
  vec2 texCoord;
  flat uint matId;
} surf;

#include "material_mesh.glsl.inc"
#include "motion_vectors.glsl.inc"

void main(void)
{
  const uint matId = uint(surf.matId);
  vec2 tc = surf.texCoord;

#if !GBUF_PASS || GBUF_NO_PREPASS
  // @TODO: rid of redundant sampling
  float alpha = get_pixel_albedo(matId, tc).w;
  // @TODO: 0.5 and proper asset
  if (alpha < 0.2)
    discard;
#endif

#if GBUF_PASS
  vec3 norm = vec3(0.f, 1.f, 0.f);
  vec4 tang = vec4(1.f, 0.f, 0.f, 1.f);

  get_pixel_gbuf_info(
    matId, norm, tang, tc,
    out_fragAlbedo, out_fragMaterial, out_fragNormal, out_fragTransmission);

  // @TEST, @TODO make param if goes well
  const float normalViewDependency = 0.5f;
  vec3 viewPos = surf.wPos - viewParams.viewPos;
  out_fragNormal =
    normalize(out_fragNormal - normalize(viewPos) * normalViewDependency);

  vec4 prevNdc = calc_prev_adjusted_viewproj_mat(viewParams, viewData) * vec4(surf.wPrevPos, 1.f);
  vec2 prevNdcXy = prevNdc.xy / prevNdc.w;

  get_static_pixel_motion_vector(
    gl_FragCoord.xy, constants.mainTargetResolution, prevNdcXy,
    out_motionVector);
#endif
}

#endif
