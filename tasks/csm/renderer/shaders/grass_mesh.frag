#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : enable

#include "geometry.h"

layout(location = 0) out vec4 out_fragAlbedo;
layout(location = 1) out vec3 out_fragMaterial;
layout(location = 2) out vec3 out_fragNormal;

layout(binding = 9, set = 0) uniform view_params_t
{
  ViewParams viewParams;
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

void main(void)
{
  const uint matId = uint(surf.matId);
  vec3 norm = normalize(surf.wNorm) * (gl_FrontFacing ? 1.f : -1.f);
  vec4 tang = surf.wTangent;
  vec2 tc = surf.texCoord;

  // @TODO: rid of redundant sampling
  float alpha = get_pixel_albedo(matId, tc).w;
  if (alpha < 0.1)
    discard;

  get_pixel_gbuf_info(
    matId, norm, tang, tc,
    out_fragAlbedo, out_fragMaterial, out_fragNormal);
}
