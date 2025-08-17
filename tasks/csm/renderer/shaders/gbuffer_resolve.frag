#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "lights.h"
#include "materials.h"
#include "constants.h"
#include "skybox.h"


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

layout(binding = 6, set = 0) uniform sampler2D gbufDepth;

layout(binding = 7, set = 0) uniform skybox_t
{
  SkyboxSourceData skybox;
};
layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};
layout(binding = 9, set = 0) uniform view_params_t
{
  ViewParams viewParams;
};

#include "bindless.glsl.inc"

layout(location = 0) in VS_OUT
{
  vec2 texCoord;
} surf;

const float POINT_SHADOW_BIAS = 0.000015f;
const float SPOT_SHADOW_BIAS = 0.000015f;
const float CSM_SHADOW_BIAS = 0.0002f;

vec3 depth_and_tc_to_pos(float depth, vec2 tc)
{
  const vec4 cameraToScreen = vec4(2.f * tc - 1.f, depth, 1.f); 
  const vec4 posHom = inverse(viewParams.mProjView) * cameraToScreen;
  return posHom.xyz / posHom.w;
}

const float EPS = 1e-5f;
const float PI = 3.14159265359f;
const float GAMMA_POW = 2.2f;
const float F_DIEL = 0.04f;

// @TODO add (back) anisotropy

#define HPLUS(v_) ((v_) > 0.f ? 1.f : 0.f)

float d_ggx(float nh, float alpha2)
{
  float nh2 = nh * nh;
  float divterm = nh2 * (alpha2 - 1.f) + 1.f;
  return (alpha2 * HPLUS(nh)) / (PI * divterm * divterm);
}

float g_smith(float nl, float nv, float hl, float hv, float alpha2)
{
  float sl = sqrt(mix(nl * nl, 1.f, alpha2));
  float sv = sqrt(mix(nv * nv, 1.f, alpha2));

  return (4.f * nl * nv * HPLUS(hl) * HPLUS(hv)) / ((nl + sl) * (nv + sv));
}

float diffuse_brdf()
{
  return 1.f / PI;
}

float specular_brdf(float nl, float nv, float hl, float hv, float nh, float a2)
{
  return (d_ggx(nh, a2) * g_smith(nl, nv, hl, hv, a2)) / max(0.001f, 4.f * nl * nv);
}

vec3 conductor_frensel_shlick(vec3 f0, float hv)
{
  return mix(f0, vec3(1.f), pow(1.f - abs(hv), 5.f));
}

vec4 shade_cook_torrance(
  vec3 n, vec3 l, vec3 v, float metalness, float roughness, vec3 albedo, vec3 lightCol)
{
  vec3 c = albedo;
  vec3 lc = lightCol;

  vec3 c_diff = mix(c, vec3(0.0f), metalness);

  vec3 nn = normalize(n);
  vec3 ll = normalize(l);
  vec3 vv = normalize(v);
  vec3 hh = normalize(ll + vv);
  float nl = max(dot(nn, ll), 0.f);
  float nv = max(dot(nn, vv), 0.f);
  float hl = max(dot(hh, ll), 0.f);
  float hv = max(dot(hh, vv), 0.f);
  float nh = max(dot(nn, hh), 0.f);

  if (nv < SHADER_EPSILON || nl < SHADER_EPSILON)
    return vec4(0.f, 0.f, 0.f, 1.f);

  float a = roughness * roughness;
  float a2 = a * a;

  vec3 f0 = mix(vec3(F_DIEL), c, metalness);
  vec3 f = conductor_frensel_shlick(f0, hv);

  float diff_bsdf = diffuse_brdf();
  float spec_bsdf = specular_brdf(nl, nv, hl, hv, nh, a2);

  vec3 diff = nl * (1.f - f) * diff_bsdf * c_diff;
  vec3 spec = nl * f * spec_bsdf;

  return vec4((spec + diff) * lc, 1.f);
}

vec3 calculate_diffuse(vec3 normal, vec3 lightDir, vec3 albedo, vec3 lightIntensity)
{
  return max(dot(normal, lightDir), 0.0f) * lightIntensity * albedo;
}

vec3 calculate_pbr(
  vec3 normal, vec3 lightDir, vec3 viewVec,
  float metalness, float roughness, vec3 albedo, vec3 lightIntensity)
{
  return shade_cook_torrance(
    normal, lightDir, viewVec, metalness, roughness, albedo, lightIntensity).xyz;
}

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

void main(void)
{
  // Unpack gbuffer
  const float depth = texture(gbufDepth, surf.texCoord).x;

  const mat4 invView = inverse(viewParams.mView);
  const vec3 camPos = invView[3].xyz / invView[3].w;

  const vec3 pos = depth_and_tc_to_pos(min(depth, 1.f), surf.texCoord);
  const vec3 viewVec = normalize(camPos - pos);
  const vec3 viewPos = (viewParams.mView * vec4(pos, 1.f)).xyz;

  if (depth >= 1.f)
  {
    if (constants.useSkybox == 0)
      out_fragColor = vec4(0.f, 0.f, 0.f, 1.f);
    else
      out_fragColor = sample_bindless_tex_cube(skybox.cubemapTexSmp, -viewVec);
    return;
  }

  const vec3 albedo = texture(gbufAlbedo, surf.texCoord).xyz;
  const vec4 matData = texture(gbufMaterial, surf.texCoord);
  const vec3 normal = texture(gbufNormal, surf.texCoord).xyz;

  const uint mat = uint(matData.x); 

  if (mat != MATERIAL_PBR && mat != MATERIAL_DIFFUSE)
  {
    out_fragColor = vec4(1.f, 0.f, 1.f, 1.f);
    return;
  }
  
  // Calculate lighting
  
  // @TODO: parametrize
  const float ambient = 0.001f;

  vec3 color = ambient * albedo;

  // For directional shadows
  int cascade = -1;
  float dirShBiasMult = 1.f;
  if (constants.useDirectionalLightShadows != 0 && lights.directionalLightsCount > 0)
  {
    for (cascade = 0; cascade < CSM_CASCADE_COUNT; ++cascade)
    {
      float prevZ = cascade == 0 ? viewParams.viewFrustum.nearZ : get_frustum_split(viewParams, cascade - 1);
      float nextZ = get_frustum_split(viewParams, cascade);

      if (viewPos.z >= prevZ && viewPos.z <= nextZ)
        break;
    }

    // @FEAT: should rather be shader asserted
    if (cascade == CSM_CASCADE_COUNT)
      --cascade;

    // @TODO: sort out properly
    dirShBiasMult = pow(float(cascade) + 0.5f, 3.0f);
  }

  // @TODO: take the cascade that we overlap w/, get the coeff from the overlap region (manhattan metric), and blend via that

  for orthoLH_ZO(uint i = 0; i < lights.directionalLightsCount; ++i)
  {
    const vec3 lightIntensity = 
      lights.directionalLights[i].color * lights.directionalLights[i].intensity;
    const vec3 lightDir = -normalize(lights.directionalLights[i].direction);

    float shadow = 1.f;

    if (constants.useDirectionalLightShadows != 0)
    {
      const vec4 posLightClipSpace = mats.directionalLightMats[i][cascade] * vec4(pos, 1.f);
      const vec3 posLightSpaceNDC = posLightClipSpace.xyz; // No perspective divide cuz ortho
      const vec2 shadowUv = posLightSpaceNDC.xy * 0.5f + 0.5f;

      const float lDepth =
        sample_bindless_tex_lod(lights.directionalLights[i].shadowmapCascades[cascade].map, shadowUv, 0.f).x +
        CSM_SHADOW_BIAS * dirShBiasMult;

      shadow = (
        shadowUv.x < SHADER_EPSILON || shadowUv.x > 1.f - SHADER_EPSILON ||
        shadowUv.y < SHADER_EPSILON || shadowUv.y > 1.f - SHADER_EPSILON ||
        lDepth < posLightSpaceNDC.z) ? 0.f : 1.f;

      if (constants.directionalLightShadowsTechnique == SHADOW_TECHNIQUE_PCF)
      {
        const int gridDim = 4; // @TODO: make a param

        const vec2 uvStep = vec2(1.f / CSM_CASCADE_RESOLUTION);
        const vec2 uvBase = shadowUv - float(gridDim) * 0.5f * uvStep;

        float sampleCount = 1.f;

        for (int y = 0; y < gridDim; ++y)
          for (int x = 0; x < gridDim; ++x)
          {
            if (x == 0 && y == 0)
              continue;

            const float lsDepth =
              sample_bindless_tex_lod(
                lights.directionalLights[i].shadowmapCascades[cascade].map,
                uvBase + uvStep * vec2(float(x), float(y)), 0.f).x +
              CSM_SHADOW_BIAS * dirShBiasMult;

            shadow += (
              shadowUv.x < SHADER_EPSILON || shadowUv.x > 1.f - SHADER_EPSILON ||
              shadowUv.y < SHADER_EPSILON || shadowUv.y > 1.f - SHADER_EPSILON ||
              lsDepth < posLightSpaceNDC.z) ? 0.f : 1.f;
            sampleCount += 1.f;
          }

        shadow /= sampleCount;
      }
    }

    if (mat == MATERIAL_PBR)
      color += shadow * calculate_pbr(normal, lightDir, viewVec, matData.y, matData.z, albedo, lightIntensity);
    else if (mat == MATERIAL_DIFFUSE)
      color += shadow * calculate_diffuse(normal, lightDir, albedo, lightIntensity);
  }

  for (uint i = 0; i < lights.pointLightsCount; ++i)
  {
    const vec3 lightIntensity = lights.pointLights[i].color * lights.pointLights[i].intensity;
    const float lightAttenuation =
      calculate_attenuation(pos, lights.pointLights[i].position, lights.pointLights[i].range);
    const vec3 lightDir = normalize(lights.pointLights[i].position - pos);
    const vec3 lightColor = lightIntensity * lightAttenuation;
    if (length(lightColor) < SHADER_EPSILON)
      continue;

    float shadow = 1.f;
    
    // @TODO: pcf

    if (constants.usePointLightShadows != 0)
    {
      const vec3 sampleDir = -vec3(lightDir.x, lightDir.y, -lightDir.z);
      const float lDepth = sample_bindless_tex_cube_lod(lights.pointLights[i].shadowmap, sampleDir, 0.f).x + POINT_SHADOW_BIAS;

      // @TODO: pull out?
      const uint faceIdx =
        abs(sampleDir.x) > abs(sampleDir.y) && abs(sampleDir.x) > abs(sampleDir.z) ? (sampleDir.x > 0.f ? 0 : 1) :
        abs(sampleDir.y) > abs(sampleDir.z) ? (sampleDir.y > 0.f ? 2 : 3) :
        (sampleDir.z > 0.f ? 4 : 5);

      const vec4 posLightClipSpace = mats.pointLightMats[i][faceIdx] * vec4(pos, 1.f);
      const vec3 posLightSpaceNDC = posLightClipSpace.xyz / posLightClipSpace.w;

      shadow = lDepth < posLightSpaceNDC.z ? 0.f : 1.f;

      if (constants.pointLightShadowsTechnique == SHADOW_TECHNIQUE_PCF)
      {
        const int gridDim = 4; // @TODO: make a param

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
            if (x == 0 && y == 0)
              continue;

            const vec3 sdir = normalize(baseDir + (float(x) - float(gridDim) * 0.5f) * ud + (float(y) - float(gridDim) * 0.5f) * vd);

            const float lsDepth =
              sample_bindless_tex_cube_lod(
                lights.pointLights[i].shadowmap, sdir, 0.f).x +
              POINT_SHADOW_BIAS;

            shadow += lsDepth < posLightSpaceNDC.z ? 0.f : 1.f;
            sampleCount += 1.f;
          }

        shadow /= sampleCount;
      }
    }

    if (mat == MATERIAL_PBR)
      color += shadow * calculate_pbr(normal, lightDir, viewVec, matData.y, matData.z, albedo, lightColor);
    else if (mat == MATERIAL_DIFFUSE)
      color += shadow * calculate_diffuse(normal, lightDir, albedo, lightColor);
  }

  for (uint i = 0; i < lights.spotLightsCount; ++i)
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
    
    // @TODO: pcf

    if (constants.useSpotLightShadows != 0)
    {
      const vec4 posLightClipSpace = mats.spotLightMats[i] * vec4(pos, 1.f);
      const vec3 posLightSpaceNDC = posLightClipSpace.xyz / posLightClipSpace.w;
      const vec2 shadowUv = vec2(-posLightSpaceNDC.x, posLightSpaceNDC.y) * 0.5f + 0.5f;

      const float lDepth = sample_bindless_tex_lod(lights.spotLights[i].shadowmap, shadowUv, 0.f).x + SPOT_SHADOW_BIAS;

      shadow = (
        shadowUv.x < SHADER_EPSILON || shadowUv.x > 1.f - SHADER_EPSILON ||
        shadowUv.y < SHADER_EPSILON || shadowUv.y > 1.f - SHADER_EPSILON ||
        lDepth < posLightSpaceNDC.z) ? 0.f : 1.f;

      // @TODO: pull out
      if (constants.spotLightShadowsTechnique == SHADOW_TECHNIQUE_PCF)
      {
        const int gridDim = 4; // @TODO: make a param

        const vec2 uvStep = vec2(1.f / SPOT_SM_RESOLUTION);
        const vec2 uvBase = shadowUv - float(gridDim) * 0.5f * uvStep;

        float sampleCount = 1.f;

        for (int y = 0; y < gridDim; ++y)
          for (int x = 0; x < gridDim; ++x)
          {
            if (x == 0 && y == 0)
              continue;

            const float lsDepth =
              sample_bindless_tex_lod(
                lights.spotLights[i].shadowmap,
                uvBase + uvStep * vec2(float(x), float(y)), 0.f).x +
              SPOT_SHADOW_BIAS;

            shadow += (
              shadowUv.x < SHADER_EPSILON || shadowUv.x > 1.f - SHADER_EPSILON ||
              shadowUv.y < SHADER_EPSILON || shadowUv.y > 1.f - SHADER_EPSILON ||
              lsDepth < posLightSpaceNDC.z) ? 0.f : 1.f;
            sampleCount += 1.f;
          }

        shadow /= sampleCount;
      }
    }

    if (mat == MATERIAL_PBR)
      color += shadow * calculate_pbr(normal, fromPosDir, viewVec, matData.y, matData.z, albedo, lightColor);
    else if (mat == MATERIAL_DIFFUSE)
      color += shadow * calculate_diffuse(normal, fromPosDir, albedo, lightColor);
  }

  out_fragColor = vec4(color, 1.0f);
}
