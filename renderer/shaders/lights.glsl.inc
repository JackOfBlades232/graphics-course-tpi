#ifndef LIGHTSINC_H_INCLUDED
#define LIGHTSINC_H_INCLUDED

#include "cpp_glsl_compat.h"

float calculate_attenuation(vec3 pos, vec3 lightPos, float range)
{
  const float dist = length(pos - lightPos);
  return max(min(1.f - pow(dist / range, 4), 1.f), 0.f) / pow(dist, 2.f);
}

float calculate_angular_attenuation(float cosine, float inner_cos, float outer_cos)
{
  float angularAttenuation = clamp((cosine - inner_cos) / (outer_cos - inner_cos), 0.f, 1.f);
  angularAttenuation = pow(angularAttenuation, 2.5f);
  return 1.f - angularAttenuation;
}

float calculate_csm_shadow_pcf_no_blend(int lid, int cid, vec3 pos)
{
  const vec4 posLightClipSpace = mats.directionalLightMats[lid][cid] * vec4(pos, 1.f);
  const vec3 posLightSpaceNDC = posLightClipSpace.xyz; // No perspective divide cuz ortho
  const vec2 shadowUv = posLightSpaceNDC.xy * 0.5f + 0.5f;

  float shadow = sample_bindless_tex_shadow_lod(
    lights.directionalLights[lid].shadowmapCascades[cid].map, vec3(shadowUv, posLightSpaceNDC.z), 0.f);

  if (SHADOW_TECHNIQUE_IS_PCF_KERNEL(constants.directionalLightShadowsTechnique))
  {
    const int gridDim = PCF_KERNEL_SIZES[constants.directionalLightShadowsTechnique];
    const int mid = gridDim / 2 + 1;

    const vec2 uvStep = vec2(1.f / CSM_CASCADE_RESOLUTION);
    const vec2 uvBase = shadowUv - float(gridDim) * 0.5f * uvStep;

    float sampleCount = 1.f;

    for (int y = 0; y < gridDim; ++y)
      for (int x = 0; x < gridDim; ++x)
      {
        if (x == mid && y == mid)
          continue;

        const vec2 uv = uvBase + uvStep * vec2(float(x), float(y));
        const float w = pcf_kernel_weight(x, y, constants.directionalLightShadowsTechnique);

        shadow += w * sample_bindless_tex_shadow_lod(
          lights.directionalLights[lid].shadowmapCascades[cid].map, vec3(uv, posLightSpaceNDC.z), 0.f);
        sampleCount += w;
      }

    shadow /= sampleCount;
  }

  return shadow;
}

float calculate_csm_shadow_pcf(int lid, int cid, vec3 pos, float z_from_start, float z_from_end)
{
  float base = calculate_csm_shadow_pcf_no_blend(lid, cid, pos);
  float prev = 0.f;
  float next = 0.f;

  float basew = 1.f;
  float prevw = 0.f;
  float nextw = 0.f;

  // @TODO: non-linear blend without more samplings (sigmoid?)
  if (cid > 0 && z_from_start <= constants.csmBlendingBeltSize)
  {
    prevw = 0.5f * (constants.csmBlendingBeltSize - z_from_start) / constants.csmBlendingBeltSize;
    prev = calculate_csm_shadow_pcf_no_blend(lid, cid - 1, pos);
  }

  if (z_from_end <= constants.csmBlendingBeltSize)
  {
    nextw = (constants.csmBlendingBeltSize - z_from_end) / constants.csmBlendingBeltSize;
    if (cid < CSM_CASCADE_COUNT - 1)
    {
      next = calculate_csm_shadow_pcf_no_blend(lid, cid + 1, pos);
      nextw *= 0.5f;
    }
    else
    {
      next = 1.f;
    }
  }

  if (prevw > 0.f || nextw > 0.f)
  {
    if (prevw > 0.f && nextw > 0.f)
    {
      prevw *= 0.5f;
      nextw *= 0.5f;
    }
    basew = 1.f - prevw - nextw;
  }

  return base * basew + prev * prevw + next * nextw;
}

struct CsmCascadeLightingData
{
  int cascade;
  float zIntoCascadeStart;
  float zIntoCascadeEnd;
  float zCascadeSize;
  float zNextCascadeSize;
  float prevSplit;
  float split;
};

CsmCascadeLightingData get_cascade_data_for_view_pos(vec3 view_pos, in ViewParams view_params)
{
  CsmCascadeLightingData data;
  data.cascade = 0;
  data.zIntoCascadeStart = 0.f;
  data.zIntoCascadeEnd = 0.f;
  data.zCascadeSize = 0.f;
  data.prevSplit = 0.f;
  data.split = view_params.viewFrustum.nearZ;
  while (data.cascade < CSM_CASCADE_COUNT)
  {
    data.zIntoCascadeStart = view_pos.z - data.split;
    data.split = get_frustum_split(view_params, data.cascade);
    data.zIntoCascadeEnd = data.split - view_pos.z;
    data.zCascadeSize = data.split - data.prevSplit;

    if (view_pos.z <= data.split)
      break;

    data.prevSplit = data.split;

    ++data.cascade;
  }

  data.zNextCascadeSize = data.zCascadeSize;
  if (data.cascade < CSM_CASCADE_COUNT - 1)
    data.zNextCascadeSize = get_frustum_split(view_params, data.cascade + 1) - data.split;

  return data;
}

vec4 get_csm_cascade_debug_multiplier(CsmCascadeLightingData data)
{
  vec4 debugMultiplier = vec4(1.f);
  if (constants.drawCascadesInSolidColor != 0)
  {
    const vec3 DEBUG_CASCADE_COLORS[4] = {
      vec3(1.f, 0.f, 0.f), vec3(0.f, 1.f, 0.f), vec3(0.f, 0.f, 1.f), vec3(0.f, 1.f, 1.f)};

    debugMultiplier = data.cascade == CSM_CASCADE_COUNT
      ? vec4(1.f, 0.f, 1.f, 1.f)
      : vec4(DEBUG_CASCADE_COLORS[data.cascade & 3], 1.f);
  }
  return debugMultiplier;
}

struct LightData
{
  vec3 intensity;
  vec3 direction;
  float shadow;
};

LightData calculate_directional_light_data(int lid, vec3 pos, in CsmCascadeLightingData data)
{
  LightData ld;
  ld.intensity = lights.directionalLights[lid].color * lights.directionalLights[lid].intensity;
  ld.direction = -normalize(lights.directionalLights[lid].direction);

  ld.shadow = 1.f;

  if (constants.useDirectionalLightShadows != 0 && data.cascade < CSM_CASCADE_COUNT)
  {
    if (SHADOW_TECHNIQUE_IS_PCF(constants.directionalLightShadowsTechnique))
    {
      ld.shadow = calculate_csm_shadow_pcf(
        lid, data.cascade, pos, data.zIntoCascadeStart / data.zCascadeSize, data.zIntoCascadeEnd / data.zNextCascadeSize);
    }
  }

  return ld;
}

#endif // LIGHTSINC_H_INCLUDED


