#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : enable
#extension GL_ARB_separate_shader_objects : enable

#include "constants.h"


layout(location = 0) out vec4 out_fragAlbedo;
layout(location = 1) out vec3 out_fragMaterial;
layout(location = 2) out vec3 out_fragNormal;

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
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

  get_pixel_gbuf_info(
    matId, surf.wNorm, surf.wTangent, surf.texCoord,
    out_fragAlbedo, out_fragMaterial, out_fragNormal);
}
