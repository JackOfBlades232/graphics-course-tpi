#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : enable

#include "materials.h"
#include "water.h"
#include "skybox.h"
#include "constants.h"
#include "quantization.h"


layout(location = 0) out vec4 out_fragColor;
layout(location = 1) out vec3 out_motionVector;

layout(binding = 3, set = 0) uniform sampler2D derivatives[WATER_CASCADE_COUNT];
layout(binding = 4, set = 0) uniform sampler2D turbulence[WATER_CASCADE_COUNT];

layout(binding = 5, set = 0) uniform sampler2D caustics;

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
  float shoreFoamFactor;
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

// @TODO: pull out
vec3 depth_and_tc_to_pos(float depth, vec2 tc)
{
  const vec4 cameraToScreen = vec4(2.f * tc - 1.f, depth, 1.f); 
  const vec4 posHom = inverse(calc_adjusted_viewproj_mat(viewParams, viewData)) * cameraToScreen;
  return posHom.xyz / posHom.w;
}

void main(void)
{
  float dydx = 0.f;
  float dydz = 0.f;
  float dxdx = 0.f;
  float dzdz = 0.f;
  float turbulence = 0.f;

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

  const float d = textureLod(opaqueDepth, surf.screenTc, 0.f).x;
  const vec3 reconstructedPos = depth_and_tc_to_pos(max(d, 0.f), surf.screenTc);

  const vec3 waterRefractionColor = vec3(0.001f, 0.05f, 0.05f);
  const vec3 waterSurfaceColor = vec3(0.165f, 0.397f, 0.491f);
  const float waterRefractionHFactor = 5.f;
  
  vec3 waterRefractedLight = waterRefractionColor;
  
  if (d > 0.f)
  {
    const vec3 refractedVector = refract_vector(-viewVec, wNormal, 1.333f);
    
    const float refractionScreenDepth = 0.5f;
    const float linz = dot(surf.wPos - viewParams.viewPos, viewParams.viewDir);
    const float screenZ = linz + refractionScreenDepth;

    const float t = (screenZ - linz) / dot(refractedVector, viewParams.viewDir);
    const vec3 intersection = surf.wPos + t * refractedVector;
    const vec4 instersectionNdc = calc_adjusted_viewproj_mat(viewParams, viewData) * vec4(intersection, 1.f);
    const vec2 intersectionUv = (instersectionNdc.xy / instersectionNdc.w) * 0.5f + 0.5f;

    const vec2 distortedTc = clamp(intersectionUv, 0.f, 1.f);
    const float dd = textureLod(opaqueDepth, distortedTc, 0.f).x;
    const vec3 distortedPos = depth_and_tc_to_pos(max(dd, 0.f), distortedTc);
    const vec2 refractionTc = distortedPos.y < surf.wPos.y ? distortedTc : surf.screenTc;

    const vec3 refractionPos = distortedPos.y < surf.wPos.y ? distortedPos : surf.wPos;

    const float rd = textureLod(opaqueDepth, refractionTc, 0.f).x;
    const vec3 rp = depth_and_tc_to_pos(max(rd, 0.f), refractionTc);

    const float depthDiff = surf.wPos.y - rp.y; 
    const float dist = length(viewParams.viewPos - rp);

    vec3 rc = textureLod(opaqueColor, refractionTc, 0.f).xyz;

    float causticFadeout = smoothstep(0.f, 1.f, clamp((150.f - dist) / 20.f, 0.f, 1.f));

    if (lights.directionalLightsCount > 0 && causticFadeout > 0.f)
    {
      const vec2 causticTexelWorldSize = vec2(0.02f); // @TODO: dedup
      const float causticApron = 0.05f;
      const float causticBody = 1.f - 2.f * causticApron;
      vec2 planarTOff = rp.xz - constants.toroidalUpdatePlayerWorldPos.xy;
      vec2 cuv = fract(vec2(0.5f) + (1.f / causticBody) * (planarTOff / causticTexelWorldSize / vec2(WATER_CAUSTIC_MAP_RES)));
      float caustic = texture(caustics, vec2(causticApron) + cuv * causticBody).x;
      rc = mix(
        rc,
        lights.directionalLights[0].color * lights.directionalLights[0].intensity * 3.f * rc,
        caustic * causticFadeout);
    }

    waterRefractedLight = mix(
      rc,
      waterRefractionColor,
      smoothstep(0.f, 1.f, clamp(depthDiff / waterRefractionHFactor, 0.f, 1.f)));
  }

  float waterRoughness = 0.1f;
  vec3 foamColor = vec3(1.f, 1.f, 1.f);
  float foamRoughness = 0.9f;

  const float foamBaseline = 2.5f;
  turbulence -= foamBaseline;
  turbulence -= surf.shoreFoamFactor;
  float foamFactor = smoothstep(0.f, 1.f, clamp(0.5f - turbulence, 0.f, 1.f));

  vec3 albedo = mix(waterSurfaceColor, foamColor, foamFactor);
  float roughness = mix(waterRoughness, foamRoughness, foamFactor);
  vec3 normal = wNormal;
  vec3 enviDir = 2.f * normal * dot(viewVec, normal) - viewVec;

  CsmCascadeLightingData csmd = get_cascade_data_for_view_pos(viewPos, viewParams);

  vec3 totSpec = vec3(0.f);

  vec3 fresnel;

  {
    vec3 spec = vec3(0.f);

    // @TODO: pull out
    vec3 n = normal;
    vec3 l = enviDir;
    vec3 v = viewVec;

    vec3 nn = normalize(n);
    vec3 ll = normalize(l);
    vec3 vv = normalize(v);
    vec3 hh = normalize(ll + vv);
    float nv = max(dot(nn, vv), 0.f);
    float hv = max(dot(hh, vv), 0.f);

    float nlu = dot(nn, ll);
    float nl = max(nlu, 0.f);

    fresnel = conductor_frensel_shlick(vec3(0.0615636836452032f), hv);
    spec = waterSurfaceColor * fresnel;

    vec3 enviColor = sample_skybox(enviDir, skybox);
    totSpec += (1.f - foamFactor) * spec * enviColor;
  }

  const vec3 ambient = mix((1.f - fresnel) * waterRefractedLight, constants.ambientLightCoeff * get_envi_ambient_from_skybox(skybox) * albedo, foamFactor);

  // @TODO: pull out
  for (int i = 0; i < lights.directionalLightsCount; ++i)
  {
    const LightData ld = calculate_directional_light_data(i, surf.wPos, csmd);
    vec3 spec = vec3(0.f);

    // @TODO: pull out
    vec3 n = normal;
    vec3 l = ld.direction;
    vec3 v = viewVec;

    vec3 nn = normalize(n);
    vec3 ll = normalize(l);
    vec3 vv = normalize(v);
    vec3 hh = normalize(ll + vv);
    float nv = max(dot(nn, vv), 0.f);
    float hl = max(dot(hh, ll), 0.f);
    float hv = max(dot(hh, vv), 0.f);
    float nh = max(dot(nn, hh), 0.f);

    float nlu = dot(nn, ll);
    float nl = max(nlu, 0.f);

    if (nv < SHADER_EPSILON || nl < SHADER_EPSILON)
    {
      continue;
    }

    float a = roughness * roughness;
    float a2 = a * a;
    vec3 f = conductor_frensel_shlick(vec3(0.0615636836452032f), hv);
    vec3 spec_bsdf = vec3(nl * specular_brdf(nl, nv, hl, hv, nh, a2));
    vec3 diff_bsdf = vec3(nl * diffuse_brdf());

    spec = f * spec_bsdf;

    totSpec += spec * ld.shadow * ld.intensity;
  }

  // @TODO: point and spot lights?

  vec4 debugMultiplier = get_csm_cascade_debug_multiplier(csmd);

  vec3 color = ambient + totSpec;
  out_fragColor = vec4(debugMultiplier.xyz * color, 1.f);

  vec4 prevNdc = calc_prev_adjusted_viewproj_mat(viewParams, viewData) * vec4(surf.wPos, 1.f);
  vec2 prevNdcXy = prevNdc.xy / prevNdc.w;

  get_static_pixel_motion_vector(
    gl_FragCoord.xy,
    constants.mainTargetResolution,
    prevNdcXy,
    get_subpixel_uv_jitter(viewParams),
    viewParams.prevSubpixelUvJitter,
    out_motionVector);

  out_motionVector.z = 0.5f;
}

