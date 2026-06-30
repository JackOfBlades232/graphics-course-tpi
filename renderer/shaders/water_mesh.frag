#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : enable

#include "materials.h"
#include "water.h"
#include "skybox.h"
#include "constants.h"
#include "quantization.h"


layout(location = 0) out vec4 out_fragColor;

layout(binding = 3, set = 0) uniform sampler2D derivatives[WATER_CASCADE_COUNT];
layout(binding = 4, set = 0) uniform sampler2D turbulence[WATER_CASCADE_COUNT];

layout(binding = 6, set = 0) uniform water_source_t
{
  WaterSourceData source;
};
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

layout(binding = 12, set = 0) uniform light_data_t
{
  UniformLights lights;
};

layout(binding = 13, set = 0) readonly buffer light_mats_t
{
  LightMatrices mats;
};

layout(binding = 14, set = 0) uniform sampler2D opaqueColor;
layout(binding = 15, set = 0) uniform sampler2D opaqueDepth;

layout(location = 0) in TE_OUT
{
  vec3 wPos;
  vec2 wInitPlanarPos;
  vec2 screenTc;
} surf;

#include "motion_vectors.glsl.inc"
#include "bindless.glsl.inc"
#include "brdf.glsl.inc"
#include "lights.glsl.inc"
#include "skybox.glsl.inc"

struct Cascade
{
  float dydx;
  float dydz;
  float dxdx;
  float dzdz;
  float turbulence;
};

Cascade sample_cascade(vec2 world_planar_pos, uint cid)
{
  float l = 0.f;
  switch (cid)
  {
  case 0:  l = source.l0; break;
  case 1:  l = source.l1; break;
  case 2:  l = source.l2; break;
  default:                break;
  }

  vec2 uv = world_planar_pos / l;

  Cascade data;
  vec4 derivatives = texture(derivatives[cid], uv);
  data.dydx = derivatives.x;
  data.dydz = derivatives.y;
  data.dxdx = derivatives.z;
  data.dzdz = derivatives.w;
  data.turbulence = texture(turbulence[cid], uv).x;

  return data;
}

void main(void)
{
  vec4 opaqueBgContrubution = vec4(0.f);

  const float d = textureLod(opaqueDepth, surf.screenTc, 0.f).x;
  if (d > 0.f)
  {
    // @TEST
    const vec3 c = textureLod(opaqueColor, surf.screenTc, 0.f).xyz;
    opaqueBgContrubution = vec4(c, 1.f);
  }

  float dydx = 0.f;
  float dydz = 0.f;
  float dxdx = 0.f;
  float dzdz = 0.f;
  float turbulence = 0.f;

  // @TEST
  for (uint cid = 0; cid < WATER_CASCADE_COUNT; ++cid)
  {
    Cascade data = sample_cascade(surf.wInitPlanarPos, cid);
    dydx += data.dydx;
    dydz += data.dydz;
    dxdx += data.dxdx;
    dzdz += data.dzdz;
    turbulence += data.turbulence;
  }

  const vec2 slope = vec2(dydx / abs(1.f + dxdx), dydz / abs(1.f + dzdz));
  const vec3 wNormal = normalize(vec3(-slope.x, 1.f, -slope.y));

  const vec3 viewVec = normalize(viewParams.viewPos - surf.wPos);
  const vec3 viewPos = (viewParams.mView * vec4(surf.wPos, 1.f)).xyz;

  // @TEST
  vec3 waterColor = vec3(0.f, 0.4f, 1.f);
  float waterRoughness = 0.2f;
  float waterAlpha = 0.85f;
  vec3 foamColor = vec3(1.f, 1.f, 1.f);
  float foamRoughness = 0.9f;
  float foamAlpha = 1.f;
  vec3 albedo = turbulence < 0.f ? foamColor : waterColor;
  float roughness = turbulence < 0.f ? foamRoughness : waterRoughness;
  float alpha = turbulence < 0.f ? foamAlpha : waterAlpha;
  vec3 normal = wNormal;

  const vec3 ambient = albedo * constants.ambientLightCoeff * get_envi_ambient_from_skybox(skybox);

  CsmCascadeLightingData csmd = get_cascade_data_for_view_pos(viewPos, viewParams);

  vec3 totDiff = vec3(0.f);
  vec3 totSpec = vec3(0.f);

  // @TODO: pull out
  for (int i = 0; i < lights.directionalLightsCount; ++i)
  {
    const LightData ld = calculate_directional_light_data(i, surf.wPos, csmd);
    vec3 diff = vec3(0.f);
    vec3 spec = vec3(0.f);
    // @TODO: proper brdf
    calculate_pbr(normal, ld.direction, viewVec, 0.f, roughness, albedo, 0.f, vec3(0.f), diff, spec);
    totDiff += diff * ld.shadow * ld.intensity;
    totSpec += spec * ld.shadow * ld.intensity;
  }

  // @TODO: point and spot lights?

  vec4 debugMultiplier = get_csm_cascade_debug_multiplier(csmd);

  vec3 color = ambient + totDiff + totSpec;
  out_fragColor = vec4(
    mix(
      debugMultiplier.xyz * color,
      opaqueBgContrubution.xyz,
      (1.f - alpha) * opaqueBgContrubution.w),
    1.f);
}

