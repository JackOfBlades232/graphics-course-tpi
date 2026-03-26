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

void main(void)
{
  // @TODO: proper impl

  // @TEST
  out_fragAlbedo = vec4(0.4f, 1.f, 0.f, 1.f);
  out_fragMaterial = vec3(0.0f); //vec3(float(MATERIAL_DIFFUSE), 0.0f, 0.0f);
  out_fragNormal = sign(dot(viewParams.mView[3].xyz - surf.wPos, surf.wNorm)) * surf.wNorm;
}
