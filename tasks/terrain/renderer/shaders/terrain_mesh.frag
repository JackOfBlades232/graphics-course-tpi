#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_ARB_separate_shader_objects : enable

#include "materials.h"
#include "terrain.h"
#include "constants.h"


// @TODO: find an appropriate way to unify with static_mesh

layout(location = 0) out vec4 out_fragAlbedo;
layout(location = 1) out vec3 out_fragMaterial;
layout(location = 2) out vec3 out_fragNormal;

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

#include "terrain_mesh.glsl.inc"

layout(location = 0) in TE_OUT
{
  vec3 wPos;
  vec2 texCoord;
} surf;

void main(void)
{
  vec3 surfaceColor;
  vec3 materialData;
  vec3 normal;

  // @TODO: albedo from clipmap
  // @TODO: normal from clipmap

  surfaceColor = vec3(0.5f, 0.9f, 0.4f);
  materialData = vec3(float(MATERIAL_DIFFUSE), 0.0f, 0.0f);
  normal = sample_normal_clipmap(surf.texCoord);

  // @TEST
  // surfaceColor *= 1000.f / length(surf.wPos - constants.playerWorldPos);

  out_fragAlbedo = vec4(surfaceColor, 1.f);
  out_fragMaterial = materialData;
  out_fragNormal = normal;
}

