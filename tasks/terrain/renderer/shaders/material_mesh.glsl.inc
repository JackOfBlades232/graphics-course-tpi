#ifndef MATERIAL_MESH_H_INCLUDED
#define MATERIAL_MESH_H_INCLUDED

#include "materials.h"
#include "quantization.h"

#include "bindless.glsl.inc"

// @TODO: optimize if need be
// @TODO: collapse code

vec4 get_pixel_albedo(uint mat_id, vec2 texcoord)
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
        surfaceColor *= sample_bindless_tex(
          materialParams[mat_id].baseColorTexSmp, texcoord).xyz;
      }
    }
    else if (materialParams[mat_id].mat == MATERIAL_DIFFUSE)
    {
      surfaceColor = dequantize4fcol(materialParams[mat_id].diffuseColorFactor).xyz;
      if (materialParams[mat_id].diffuseTexSmp != NO_TEXTURE_ID)
      {
        surfaceColor *= sample_bindless_tex(
          materialParams[mat_id].diffuseTexSmp, texcoord).xyz;
      }
    }
  }

  return vec4(surfaceColor, 1.f);
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
      materialData = vec3(float(MATERIAL_DIFFUSE), 0.0f, 0.0f);
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
      materialData = vec3(float(MATERIAL_DIFFUSE), 0.0f, 0.0f);
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

void get_pixel_gbuf_info(
  uint mat_id, vec3 base_normal, vec4 base_tangent, vec2 texcoord,
  out vec4 out_albedo, out vec3 out_matdata, out vec3 out_normal)
{
  out_albedo = get_pixel_albedo(mat_id, texcoord);
  out_matdata = get_pixel_matdata(mat_id, texcoord);
  out_normal = get_pixel_normal(mat_id, base_normal, base_tangent, texcoord);
}

#endif // MATERIAL_MESH_H_INCLUDED

