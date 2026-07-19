#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "lights.h"
#include "materials.h"
#include "constants.h"
#include "skybox.h"
#include "quantization.h"


layout(location = 0) out vec4 out_fragColor;

layout(binding = 1, set = 0) uniform light_data_t
{
  UniformLights lights;
};

layout(binding = 2, set = 0) readonly buffer light_mats_t
{
  LightMatrices mats;
};

layout(binding = 3, set = 0) uniform sampler2D gbufAlbedo;
layout(binding = 4, set = 0) uniform sampler2D gbufMaterial;
layout(binding = 5, set = 0) uniform sampler2D gbufNormal;
layout(binding = 6, set = 0) uniform sampler2D gbufTransmission;

layout(binding = 7, set = 0) uniform sampler2D gbufDepth;

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

layout(binding = 11, set = 0) uniform skybox_t
{
  SkyboxSourceData skybox;
};

layout(binding = 12, set = 0) uniform sampler2D aoBuffer;

#include "bindless.glsl.inc"
#include "brdf.glsl.inc"
#include "lights.glsl.inc"
#include "skybox.glsl.inc"

layout(location = 0) in VS_OUT
{
  vec2 texCoord;
} surf;

vec3 depth_and_tc_to_pos(float depth, vec2 tc)
{
  const vec4 cameraToScreen = vec4(2.f * tc - 1.f, depth, 1.f); 
  const vec4 posHom = inverse(calc_adjusted_viewproj_mat(viewParams, viewData)) * cameraToScreen;
  return posHom.xyz / posHom.w;
}

void main(void)
{
  // Unpack gbuffer
  const float depth = texture(gbufDepth, surf.texCoord).x;

  const vec3 reconstructedPos = depth_and_tc_to_pos(max(depth, 0.f), surf.texCoord);
  const vec3 pos = reconstructedPos; //texture(gbufPos, surf.texCoord).xyz;
  const vec3 viewVec = normalize(viewParams.viewPos - pos);
  const vec3 viewPos = (viewParams.mView * vec4(pos, 1.f)).xyz;

  if (depth <= 0.f)
  {
    // @TODO: unjitter skybox UV
    out_fragColor = vec4(sample_sky_light(normalize(reconstructedPos - viewParams.viewPos), skybox), 1.f);
    return;
  }

  const vec3 albedo = texture(gbufAlbedo, surf.texCoord).xyz;
  const vec4 matData = texture(gbufMaterial, surf.texCoord);
  const vec3 normal = texture(gbufNormal, surf.texCoord).xyz;
  const vec4 transData = texture(gbufTransmission, surf.texCoord);

  float ao = 1.f;
  if (constants.useSsao != 0)
    ao = texture(aoBuffer, surf.texCoord).x;

  const uint mat = uint(matData.x + 0.001f);
  
  // Calculate lighting
  
  const vec3 ambient = ao * albedo * constants.ambientLightCoeff * get_envi_ambient_from_skybox(skybox);

  // For directional shadows
  CsmCascadeLightingData csmd = get_cascade_data_for_view_pos(viewPos, viewParams);

  vec4 debugMultiplier = get_csm_cascade_debug_multiplier(csmd);

  if (mat != MATERIAL_PBR && mat != MATERIAL_DIFFUSE)
  {
    out_fragColor = debugMultiplier * vec4(1.f, 0.f, 1.f, 1.f);
    return;
  }

  vec3 totDiff = vec3(0.f);
  vec3 totSpec = vec3(0.f);

  // @TODO: take the cascade that we overlap w/, get the coeff from the overlap region (manhattan metric), and blend via that

  for (int i = 0; i < lights.directionalLightsCount; ++i)
  {
    const LightData ld = calculate_directional_light_data(i, pos, csmd);
    if (length(ld.intensity) < SHADER_EPSILON)
      continue;

    vec3 diff = vec3(0.f);
    vec3 spec = vec3(0.f);

    if (mat == MATERIAL_PBR)
      calculate_pbr(normal, ld.direction, viewVec, matData.y, matData.z, albedo, transData.w, transData.xyz, diff, spec);
    else if (mat == MATERIAL_DIFFUSE)
      calculate_pbr_diff_spec_gloss(normal, ld.direction, viewVec, albedo, dequantize4fcol(floatBitsToUint(matData.y)).xyz, matData.z, transData.w, transData.xyz, diff, spec);

    totDiff += diff * ld.shadow * ld.intensity;
    totSpec += spec * ld.shadow * ld.intensity;
  }

  // @TODO: refactor to LightData
  for (int i = 0; i < lights.pointLightsCount; ++i)
  {
    const LightData ld = calculate_point_light_data(i, pos);
    if (length(ld.intensity) < SHADER_EPSILON)
      continue;

    vec3 diff = vec3(0.f);
    vec3 spec = vec3(0.f);

    if (mat == MATERIAL_PBR)
      calculate_pbr(normal, ld.direction, viewVec, matData.y, matData.z, albedo, transData.w, transData.xyz, diff, spec);
    else if (mat == MATERIAL_DIFFUSE)
      calculate_pbr_diff_spec_gloss(normal, ld.direction, viewVec, albedo, dequantize4fcol(floatBitsToUint(matData.y)).xyz, matData.z, transData.w, transData.xyz, diff, spec);

    totDiff += diff * ld.shadow * ld.intensity;
    totSpec += spec * ld.shadow * ld.intensity;
  }

  for (int i = 0; i < lights.spotLightsCount; ++i)
  {
    const LightData ld = calculate_spot_light_data(i, pos);
    if (length(ld.intensity) < SHADER_EPSILON)
      continue;

    vec3 diff = vec3(0.f);
    vec3 spec = vec3(0.f);

    if (mat == MATERIAL_PBR)
      calculate_pbr(normal, ld.direction, viewVec, matData.y, matData.z, albedo, transData.w, transData.xyz, diff, spec);
    else if (mat == MATERIAL_DIFFUSE)
      calculate_pbr_diff_spec_gloss(normal, ld.direction, viewVec, albedo, dequantize4fcol(floatBitsToUint(matData.y)).xyz, matData.z, transData.w, transData.xyz, diff, spec);

    totDiff += diff * ld.shadow * ld.intensity;
    totSpec += spec * ld.shadow * ld.intensity;
  }

  vec3 color = ambient + totDiff + totSpec;

  out_fragColor = debugMultiplier * vec4(color, 1.0f);
}
