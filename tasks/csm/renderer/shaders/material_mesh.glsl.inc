#ifndef MATERIAL_MESH_H_INCLUDED
#define MATERIAL_MESH_H_INCLUDED

#include "materials.h"
#include "quantization.h"

#include "bindless.glsl.inc"

// @TODO: optimize if need be
// @TODO: collapse code

vec4 get_pixel_albedo(uint mat_id, vec2 texcoord)
{
  vec4 surfaceColor;

  if (mat_id == NO_MATERIAL)
  {
    surfaceColor = vec4(1.f, 0.f, 1.f, 1.f);
  }
  else
  {
    if (materialParams[mat_id].mat == MATERIAL_PBR)
    {
      surfaceColor = dequantize4fcol(materialParams[mat_id].baseColorFactor);
      if (materialParams[mat_id].baseColorTexSmp != NO_TEXTURE_ID)
      {
        surfaceColor *= sample_bindless_tex(
          materialParams[mat_id].baseColorTexSmp, texcoord);
      }
    }
    else if (materialParams[mat_id].mat == MATERIAL_DIFFUSE)
    {
      surfaceColor = dequantize4fcol(materialParams[mat_id].diffuseColorFactor);
      if (materialParams[mat_id].diffuseTexSmp != NO_TEXTURE_ID)
      {
        surfaceColor *= sample_bindless_tex(
          materialParams[mat_id].diffuseTexSmp, texcoord);
      }
    }
  }

  return surfaceColor;
}

vec4 get_pixel_albedo_lod(uint mat_id, vec2 texcoord, float lod)
{
  vec3 surfaceColor;

  if (mat_id == NO_MATERIAL)
  {
    surfaceColor = vec3(1.f, 0.f, 1.f);
  }
  else
  {
    if (materialParams[mat_id].mat == MATERIAL_PBR)
    {
      surfaceColor = dequantize4fcol(materialParams[mat_id].baseColorFactor).xyz;
      if (materialParams[mat_id].baseColorTexSmp != NO_TEXTURE_ID)
      {
        surfaceColor *= sample_bindless_tex_lod(
          materialParams[mat_id].baseColorTexSmp, texcoord, lod).xyz;
      }
    }
    else if (materialParams[mat_id].mat == MATERIAL_DIFFUSE)
    {
      surfaceColor = dequantize4fcol(materialParams[mat_id].diffuseColorFactor).xyz;
      if (materialParams[mat_id].diffuseTexSmp != NO_TEXTURE_ID)
      {
        surfaceColor *= sample_bindless_tex_lod(
          materialParams[mat_id].diffuseTexSmp, texcoord, lod).xyz;
      }
    }
  }

  return vec4(surfaceColor, 1.f);
}

vec3 get_pixel_matdata(uint mat_id, vec2 texcoord)
{
  vec3 materialData;

  if (mat_id == NO_MATERIAL)
  {
    materialData = vec3(0.0f);
  }
  else
  {
    if (materialParams[mat_id].mat == MATERIAL_PBR)
    {
      float metalness = materialParams[mat_id].metalnessFactor;
      float roughness = materialParams[mat_id].roughnessFactor;

      if (materialParams[mat_id].metalnessRoughnessTexSmp != NO_TEXTURE_ID)
      {
        vec2 mr = sample_bindless_tex(
          materialParams[mat_id].metalnessRoughnessTexSmp, texcoord).xy;
        metalness *= mr.x;
        roughness *= mr.y;
      }

      materialData = vec3(float(MATERIAL_PBR), metalness, roughness);
    }
    else if (materialParams[mat_id].mat == MATERIAL_DIFFUSE)
    {
      vec3 specularColor = dequantize4fcol(materialParams[mat_id].specularFactor).xyz;
      float glossiness = materialParams[mat_id].glossinessFactor;

      if (materialParams[mat_id].specularGlossinessTexSmp != NO_TEXTURE_ID)
      {
        vec4 specGlossPacked = sample_bindless_tex(
          materialParams[mat_id].specularGlossinessTexSmp, texcoord);
        specularColor *= specGlossPacked.xyz;
        glossiness *= specGlossPacked.w;
      }

      materialData = vec3(
        float(MATERIAL_DIFFUSE),
        uintBitsToFloat(quantize4fcol(vec4(specularColor, 0.f))),
        glossiness);
    }
  }

  return materialData;
}

vec3 get_pixel_matdata_lod(uint mat_id, vec2 texcoord, float lod)
{
  vec3 materialData;

  if (mat_id == NO_MATERIAL)
  {
    materialData = vec3(0.0f);
  }
  else
  {
    if (materialParams[mat_id].mat == MATERIAL_PBR)
    {
      float metalness = materialParams[mat_id].metalnessFactor;
      float roughness = materialParams[mat_id].roughnessFactor;

      if (materialParams[mat_id].metalnessRoughnessTexSmp != NO_TEXTURE_ID)
      {
        vec2 mr = sample_bindless_tex_lod(
          materialParams[mat_id].metalnessRoughnessTexSmp, texcoord, lod).xy;
        metalness *= mr.x;
        roughness *= mr.y;
      }

      materialData = vec3(float(MATERIAL_PBR), metalness, roughness);
    }
    else if (materialParams[mat_id].mat == MATERIAL_DIFFUSE)
    {
      vec3 specularColor = dequantize4fcol(materialParams[mat_id].specularFactor).xyz;
      float glossiness = materialParams[mat_id].glossinessFactor;

      if (materialParams[mat_id].specularGlossinessTexSmp != NO_TEXTURE_ID)
      {
        vec4 specGlossPacked = sample_bindless_tex_lod(
          materialParams[mat_id].specularGlossinessTexSmp, texcoord, lod);
        specularColor *= specGlossPacked.xyz;
        glossiness *= specGlossPacked.w;
      }

      materialData = vec3(
        float(MATERIAL_DIFFUSE),
        uintBitsToFloat(quantize4fcol(vec4(specularColor, 0.f))),
        glossiness);
    }
  }

  return materialData;
}

vec3 get_pixel_normal(
  uint mat_id, vec3 base_normal, vec4 base_tangent, vec2 texcoord)
{
  vec3 normal;

  if (mat_id == NO_MATERIAL)
  {
    normal = vec3(0.0f);
  }
  else
  {
    if (materialParams[mat_id].normalTexSmp != NO_TEXTURE_ID)
    {
      const vec3 norm = normalize(base_normal);
      const vec3 sampledNormal =
        2.f * sample_bindless_tex(materialParams[mat_id].normalTexSmp, texcoord).xyz - 1.f;

      const vec3 tang = base_tangent.xyz;
      const vec3 ptang = normalize(tang - norm * dot(tang, norm));
      const vec3 bitang = cross(norm, ptang) * base_tangent.w;

      normal = normalize(
        sampledNormal.x * ptang + sampledNormal.y * bitang + sampledNormal.z * norm);
    }
    else
      normal = normalize(base_normal);
  }

  return normal;
}

vec3 get_pixel_normal_lod(
  uint mat_id, vec3 base_normal, vec4 base_tangent, vec2 texcoord, float lod)
{
  vec3 normal;

  if (mat_id == NO_MATERIAL)
  {
    normal = vec3(0.0f);
  }
  else
  {
    if (materialParams[mat_id].normalTexSmp != NO_TEXTURE_ID)
    {
      const vec3 norm = normalize(base_normal);
      const vec3 sampledNormal =
        2.f * sample_bindless_tex_lod(materialParams[mat_id].normalTexSmp, texcoord, lod).xyz - 1.f;

      const vec3 tang = base_tangent.xyz;
      const vec3 ptang = normalize(tang - norm * dot(tang, norm));
      const vec3 bitang = cross(norm, ptang) * base_tangent.w;

      normal = normalize(
        sampledNormal.x * ptang + sampledNormal.y * bitang + sampledNormal.z * norm);
    }
    else
      normal = normalize(base_normal);
  }

  return normal;
}

vec4 get_pixel_transmission(uint mat_id, vec2 texcoord, vec4 pixel_albedo)
{
  vec4 transmissionParams;

  if (mat_id != NO_MATERIAL && materialParams[mat_id].diffuseTransmissionFactor > SHADER_EPSILON)
  {
    float transFactor = materialParams[mat_id].diffuseTransmissionFactor;
    vec3 transColor = dequantize4fcol(materialParams[mat_id].diffuseTransmissionColorFactor).xyz;
    if (materialParams[mat_id].diffuseTransmissionTexSmp != NO_TEXTURE_ID)
    {
      transFactor *= sample_bindless_tex(
        materialParams[mat_id].diffuseTransmissionTexSmp, texcoord).a;
    }
    if (constants.useAlbedoAsDiffuseTransmissionColor != 0)
    {
      transColor *= pixel_albedo.xyz;
    }
    if (materialParams[mat_id].diffuseTransmissionColorTexSmp != NO_TEXTURE_ID)
    {
      transColor *= sample_bindless_tex(
        materialParams[mat_id].diffuseTransmissionColorTexSmp, texcoord).xyz;
    }
    transmissionParams = vec4(transColor, transFactor);
  }
  else
  {
    transmissionParams = vec4(1.f, 1.f, 1.f, 0.f);
  }

  return transmissionParams;
}

vec4 get_pixel_transmission_lod(uint mat_id, vec2 texcoord, vec4 pixel_albedo, float lod)
{
  vec4 transmissionParams;

  if (mat_id != NO_MATERIAL && materialParams[mat_id].diffuseTransmissionFactor > SHADER_EPSILON)
  {
    float transFactor = materialParams[mat_id].diffuseTransmissionFactor;
    vec3 transColor = dequantize4fcol(materialParams[mat_id].diffuseTransmissionColorFactor).xyz;
    if (materialParams[mat_id].diffuseTransmissionTexSmp != NO_TEXTURE_ID)
    {
      transFactor *= sample_bindless_tex_lod(
        materialParams[mat_id].diffuseTransmissionTexSmp, texcoord, lod).a;
    }
    if (constants.useAlbedoAsDiffuseTransmissionColor != 0)
    {
      transColor *= pixel_albedo.xyz;
    }
    if (materialParams[mat_id].diffuseTransmissionColorTexSmp != NO_TEXTURE_ID)
    {
      transColor *= sample_bindless_tex_lod(
        materialParams[mat_id].diffuseTransmissionColorTexSmp, texcoord, lod).xyz;
    }
    transmissionParams = vec4(transColor, transFactor);
  }
  else
  {
    transmissionParams = vec4(1.f, 1.f, 1.f, 0.f);
  }

  return transmissionParams;
}

void get_pixel_gbuf_info(
  uint mat_id, vec3 base_normal, vec4 base_tangent, vec2 texcoord,
  out vec4 out_albedo, out vec3 out_matdata, out vec3 out_normal, out vec4 out_transmission)
{
  out_albedo = get_pixel_albedo(mat_id, texcoord);
  out_matdata = get_pixel_matdata(mat_id, texcoord);
  out_normal = get_pixel_normal(mat_id, base_normal, base_tangent, texcoord);
  out_transmission = get_pixel_transmission(mat_id, texcoord, out_albedo);
}

#endif // MATERIAL_MESH_H_INCLUDED

