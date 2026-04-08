#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : enable

#include "geometry.h"
#include "constants.h"

layout(location = 0) out vec4 out_fragAlbedo;
layout(location = 1) out vec3 out_fragMaterial;
layout(location = 2) out vec3 out_fragNormal;
layout(location = 3) out vec4 out_fragTransmission;

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};
layout(binding = 9, set = 0) uniform view_params_t
{
  ViewParams viewParams;
};

layout(location = 0) in VS_OUT
{
  vec3 wPos;
  vec2 texCoord;
  flat uint matId;
} surf;

#include "material_mesh.glsl.inc"

void main(void)
{
  const uint matId = uint(surf.matId);
  vec3 norm = vec3(0.f, 1.f, 0.f);
  vec4 tang = vec4(1.f, 0.f, 0.f, 1.f);
  vec2 tc = surf.texCoord;

  // @TODO: rid of redundant sampling
  float alpha = get_pixel_albedo(matId, tc).w;
  // @TODO: 0.5 and proper asset
  if (alpha < 0.2)
    discard;

  get_pixel_gbuf_info(
    matId, norm, tang, tc,
    out_fragAlbedo, out_fragMaterial, out_fragNormal, out_fragTransmission);

  // @TEST, @TODO make param if goes well
  const float normalViewDependency = 0.5f;
  vec3 viewPos = surf.wPos - viewParams.mViewPos;
  out_fragNormal =
    normalize(out_fragNormal - normalize(viewPos) * normalViewDependency);
}
