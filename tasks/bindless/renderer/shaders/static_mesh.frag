#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require
#extension GL_EXT_nonuniform_qualifier : require

#include "materials.h"
#include "quantization.h"


layout(location = 0) out vec4 out_fragAlbedo;
layout(location = 1) out vec3 out_fragMaterial;
layout(location = 2) out vec3 out_fragNormal;

layout(push_constant) uniform params_t
{
  mat4 mProjView;
  mat4 mModelAndMatId;
} params;

layout(binding = 0, set = 0) uniform texture2D bindlessTextures[MAX_BINDLESS_TEXTURES];
layout(binding = 1, set = 0) uniform sampler bindlessSamplers[MAX_BINDLESS_SAMPLERS];
layout(binding = 2, set = 0) readonly buffer material_params_t
{
  Material materialParams[];
};

layout(location = 0) in VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec4 wTangent;
  vec2 texCoord;
} surf;

vec4 sample_bindless_tex(TexSmpIdPair ids, vec2 uv)
{
  if (ids == NO_TEXTURE_ID)
    return vec4(1.0f);

  const uvec2 texSmpIds = unpack_tex_smp_id_pair(ids);
  return texture(sampler2D(bindlessTextures[texSmpIds.x], bindlessSamplers[texSmpIds.y]), uv);
}

void main()
{
  vec3 surfaceColor;
  vec3 materialData;
  vec3 normal;

  const uint matId = floatBitsToUint(params.mModelAndMatId[3].w);

  if (matId == NO_MATERIAL)
  {
    surfaceColor = vec3(1.f, 0.f, 1.f);
    materialData = vec3(0.0f);
    normal = vec3(0.0f);
  }
  else
  {
    if (materialParams[matId].normalTexSmp != NO_TEXTURE_ID)
    {
      const vec3 norm = normalize(surf.wNorm);
      const vec3 sampledNormal =
        2.f * sample_bindless_tex(materialParams[matId].normalTexSmp, surf.texCoord).xyz - 1.f;

      const vec3 tang = surf.wTangent.xyz;
      const vec3 ptang = normalize(tang - norm * dot(tang, norm));
      const vec3 bitang = cross(norm, ptang) * surf.wTangent.w;

      normal = normalize(
        sampledNormal.x * ptang + sampledNormal.y * bitang + sampledNormal.z * norm);
    }
    else
      normal = normalize(surf.wNorm);

    if (materialParams[matId].mat == MATERIAL_PBR)
    {
      surfaceColor = dequantize4fcol(materialParams[matId].baseColorFactor).xyz;
      float metalness = materialParams[matId].metalnessFactor;
      float roughness = materialParams[matId].roughnessFactor;

      if (materialParams[matId].baseColorTexSmp != NO_TEXTURE_ID)
        surfaceColor *= sample_bindless_tex(
          materialParams[matId].baseColorTexSmp, surf.texCoord).xyz;
      if (materialParams[matId].metalnessRoughnessTexSmp != NO_TEXTURE_ID)
      {
        vec2 mr = sample_bindless_tex(
          materialParams[matId].metalnessRoughnessTexSmp, surf.texCoord).xy;
        metalness *= mr.x;
        roughness *= mr.y;
      }

      materialData = vec3(float(MATERIAL_PBR), metalness, roughness);
    }
    else if (materialParams[matId].mat == MATERIAL_DIFFUSE)
    {
      surfaceColor = dequantize4fcol(materialParams[matId].diffuseColorFactor).xyz;
      if (materialParams[matId].diffuseTexSmp != NO_TEXTURE_ID)
      {
        surfaceColor *= sample_bindless_tex(
          materialParams[matId].diffuseTexSmp, surf.texCoord).xyz;
      }

      materialData = vec3(float(MATERIAL_DIFFUSE), 0.0f, 0.0f);
    }
  }

  out_fragAlbedo = vec4(surfaceColor, 1.f);
  out_fragMaterial = materialData;
  out_fragNormal = normal;
}
