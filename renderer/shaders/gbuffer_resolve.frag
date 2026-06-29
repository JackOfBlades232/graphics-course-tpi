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
    vec3 skyColor = vec3(0.f);
    vec3 sunColor = vec3(0.f);
    // @TODO: unjitter skybox UV
    vec3 dirToSky = normalize(reconstructedPos - viewParams.viewPos); 
    if (constants.useSkybox != 0)
    {
      skyColor = sample_bindless_tex_cube(skybox.cubemapTexSmp, dirToSky).xyz;
    }
    if (lights.directionalLightsCount > 0)
    {
      vec3 dirToSun = -normalize(lights.directionalLights[0].direction);
      vec3 sunCol = lights.directionalLights[0].intensity * lights.directionalLights[0].color;
      float cosToSky = clamp(dot(dirToSky, dirToSun), 0.f, 1.f);
      sunColor = sunCol * pow(cosToSky, 3500.f);
    }
    out_fragColor = vec4(skyColor + sunColor, 1.f);
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
  
  vec3 ambientCol = constants.ambientLightCoeff;
  if (constants.useSkybox != 0 && constants.useSkyboxForAmbient != 0)
  {
    float maxLod = floor(log2(bindless_tex_cube_size(skybox.cubemapTexSmp).x));
    ambientCol *= sample_bindless_tex_cube_lod(skybox.cubemapTexSmp, vec3(0.f, 1.f, 0.f), maxLod).xyz;
  }

  const vec3 ambient = ambientCol * ao * albedo;

  vec4 debugMultiplier = vec4(1.f);

  // For directional shadows
  CsmCascadeLightingData csmd = get_cascade_data_for_view_pos(viewPos, viewParams);

  if (constants.drawCascadesInSolidColor != 0)
  {
    const vec3 DEBUG_CASCADE_COLORS[4] = {
      vec3(1.f, 0.f, 0.f), vec3(0.f, 1.f, 0.f), vec3(0.f, 0.f, 1.f), vec3(0.f, 1.f, 1.f)};

    debugMultiplier = csmd.cascade == CSM_CASCADE_COUNT
      ? vec4(1.f, 0.f, 1.f, 1.f)
      : vec4(DEBUG_CASCADE_COLORS[csmd.cascade & 3], 1.f);
  }

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
    const vec3 lightIntensity = 
      lights.directionalLights[i].color * lights.directionalLights[i].intensity;
    const vec3 lightDir = -normalize(lights.directionalLights[i].direction);

    float shadow = 1.f;

    if (constants.useDirectionalLightShadows != 0 && csmd.cascade < CSM_CASCADE_COUNT)
    {
      if (SHADOW_TECHNIQUE_IS_PCF(constants.directionalLightShadowsTechnique))
      {
        shadow = calculate_csm_shadow_pcf(
          i, csmd.cascade, pos, csmd.zIntoCascadeStart / csmd.zCascadeSize, csmd.zIntoCascadeEnd / csmd.zNextCascadeSize);
      }
    }

    vec3 diff = vec3(0.f);
    vec3 spec = vec3(0.f);

    if (mat == MATERIAL_PBR)
      calculate_pbr(normal, lightDir, viewVec, matData.y, matData.z, albedo, transData.w, transData.xyz, diff, spec);
    else if (mat == MATERIAL_DIFFUSE)
      calculate_pbr_diff_spec_gloss(normal, lightDir, viewVec, albedo, dequantize4fcol(floatBitsToUint(matData.y)).xyz, matData.z, transData.w, transData.xyz, diff, spec);

    totDiff += diff * shadow * lightIntensity;
    totSpec += spec * shadow * lightIntensity;
  }

  for (int i = 0; i < lights.pointLightsCount; ++i)
  {
    const vec3 lightIntensity = lights.pointLights[i].color * lights.pointLights[i].intensity;
    const float lightAttenuation =
      calculate_attenuation(pos, lights.pointLights[i].position, lights.pointLights[i].range);
    const vec3 lightDir = normalize(lights.pointLights[i].position - pos);
    const vec3 lightColor = lightIntensity * lightAttenuation;
    if (length(lightColor) < SHADER_EPSILON)
      continue;

    float shadow = 1.f;

    if (constants.usePointLightShadows != 0)
    {
      const vec3 sampleDir = -vec3(lightDir.x, lightDir.y, -lightDir.z);

      // @TODO: pull out?
      const uint faceIdx =
        abs(sampleDir.x) > abs(sampleDir.y) && abs(sampleDir.x) > abs(sampleDir.z) ? (sampleDir.x > 0.f ? 0 : 1) :
        abs(sampleDir.y) > abs(sampleDir.z) ? (sampleDir.y > 0.f ? 2 : 3) :
        (sampleDir.z > 0.f ? 4 : 5);

      const vec4 posLightClipSpace = mats.pointLightMats[i][faceIdx] * vec4(pos, 1.f);
      const vec3 posLightSpaceNDC = posLightClipSpace.xyz / posLightClipSpace.w;

      if (SHADOW_TECHNIQUE_IS_PCF(constants.pointLightShadowsTechnique))
      {
        shadow = sample_bindless_tex_cube_shadow_lod(
          lights.pointLights[i].shadowmap, vec4(sampleDir, posLightSpaceNDC.z), 0.f);

        if (SHADOW_TECHNIQUE_IS_PCF_KERNEL(constants.pointLightShadowsTechnique))
        {
          const int gridDim = PCF_KERNEL_SIZES[constants.pointLightShadowsTechnique];
          const int mid = gridDim / 2 + 1;

          const float faceExt =
            (faceIdx == 0 || faceIdx == 1) ? abs(sampleDir.x) :
            (faceIdx == 2 || faceIdx == 3) ? abs(sampleDir.y) :
            abs(sampleDir.z);

          const vec3 baseDir = sampleDir / faceExt;

          // PCF is symmetrical => dir does not matter
          const vec3 ud = (2.f / float(POINT_SM_RESOLUTION)) * ((faceIdx == 2 || faceIdx == 3) ? vec3(1.f, 0.f, 0.f) : vec3(0.f, 1.f, 0.f));
          const vec3 vd = (2.f / float(POINT_SM_RESOLUTION)) * ((faceIdx == 4 || faceIdx == 5) ? vec3(1.f, 0.f, 0.f) : vec3(0.f, 0.f, 1.f));

          float sampleCount = 1.f;

          for (int y = 0; y < gridDim; ++y)
            for (int x = 0; x < gridDim; ++x)
            {
              if (x == mid && y == mid)
                continue;

              const vec3 sdir = normalize(baseDir + (float(x) - float(gridDim) * 0.5f) * ud + (float(y) - float(gridDim) * 0.5f) * vd);
              const float w = pcf_kernel_weight(x, y, constants.pointLightShadowsTechnique);

              shadow += w * sample_bindless_tex_cube_shadow_lod(
                lights.pointLights[i].shadowmap, vec4(sdir, posLightSpaceNDC.z), 0.f);
              sampleCount += w;
            }

          shadow /= sampleCount;
        }
      }
    }

    vec3 diff = vec3(0.f);
    vec3 spec = vec3(0.f);

    if (mat == MATERIAL_PBR)
      calculate_pbr(normal, lightDir, viewVec, matData.y, matData.z, albedo, transData.w, transData.xyz, diff, spec);
    else if (mat == MATERIAL_DIFFUSE)
      calculate_pbr_diff_spec_gloss(normal, lightDir, viewVec, albedo, dequantize4fcol(floatBitsToUint(matData.y)).xyz, matData.z, transData.w, transData.xyz, diff, spec);

    totDiff += diff * shadow * lightColor;
    totSpec += spec * shadow * lightColor;
  }

  for (int i = 0; i < lights.spotLightsCount; ++i)
  {
    const vec3 lightIntensity = lights.spotLights[i].color * lights.spotLights[i].intensity;
    const float lightAttenuation =
      calculate_attenuation(pos, lights.spotLights[i].position, lights.spotLights[i].range);

    const vec3 lightDir = normalize(lights.spotLights[i].direction);
    const vec3 fromPosDir = normalize(lights.spotLights[i].position - pos);

    const float lightAngularAttenuation = calculate_angular_attenuation(
      dot(lightDir, -fromPosDir),
      cos(lights.spotLights[i].innerConeAngle * 0.5f),
      cos(lights.spotLights[i].outerConeAngle * 0.5f));

    const vec3 lightColor = lightIntensity * lightAttenuation * lightAngularAttenuation;
    if (length(lightColor) < SHADER_EPSILON)
      continue;

    float shadow = 1.f;
    
    if (constants.useSpotLightShadows != 0)
    {
      const vec4 posLightClipSpace = mats.spotLightMats[i] * vec4(pos, 1.f);
      const vec3 posLightSpaceNDC = posLightClipSpace.xyz / posLightClipSpace.w;
      const vec2 shadowUv = posLightSpaceNDC.xy * 0.5f + 0.5f;

      if (SHADOW_TECHNIQUE_IS_PCF(constants.spotLightShadowsTechnique))
      {
        shadow = sample_bindless_tex_shadow_lod(
          lights.spotLights[i].shadowmap, vec3(shadowUv, posLightSpaceNDC.z), 0.f);

        // @TODO: pull out
        if (SHADOW_TECHNIQUE_IS_PCF_KERNEL(constants.spotLightShadowsTechnique))
        {
          const int gridDim = PCF_KERNEL_SIZES[constants.spotLightShadowsTechnique];
          const int mid = gridDim / 2 + 1;

          const vec2 uvStep = vec2(1.f / SPOT_SM_RESOLUTION);
          const vec2 uvBase = shadowUv - float(gridDim) * 0.5f * uvStep;

          float sampleCount = 1.f;

          for (int y = 0; y < gridDim; ++y)
            for (int x = 0; x < gridDim; ++x)
            {
              if (x == mid && y == mid)
                continue;

              const vec2 uv = uvBase + uvStep * vec2(float(x), float(y));
              const float w = pcf_kernel_weight(x, y, constants.spotLightShadowsTechnique);

              shadow += w * sample_bindless_tex_shadow_lod(
                lights.spotLights[i].shadowmap, vec3(uv, posLightSpaceNDC.z), 0.f);
              sampleCount += w;
            }

          shadow /= sampleCount;
        }
      }
    }

    vec3 diff = vec3(0.f);
    vec3 spec = vec3(0.f);

    if (mat == MATERIAL_PBR)
      calculate_pbr(normal, lightDir, viewVec, matData.y, matData.z, albedo, transData.w, transData.xyz, diff, spec);
    else if (mat == MATERIAL_DIFFUSE)
      calculate_pbr_diff_spec_gloss(normal, lightDir, viewVec, albedo, dequantize4fcol(floatBitsToUint(matData.y)).xyz, matData.z, transData.w, transData.xyz, diff, spec);

    totDiff += diff * shadow * lightColor;
    totSpec += spec * shadow * lightColor;
  }

  vec3 color = ambient + totDiff + totSpec;

  out_fragColor = debugMultiplier * vec4(color, 1.0f);
}
