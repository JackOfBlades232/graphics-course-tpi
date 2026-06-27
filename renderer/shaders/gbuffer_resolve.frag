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

// @TODO dedup with tonemap
float luminance_bt601(vec3 col)
{
  return 0.299f * col.r + 0.587f * col.g + 0.114f * col.b;
}

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

const float EPS = 1e-5f;
const float PI = 3.14159265359f;
const float GAMMA_POW = 2.2f;
const float F_DIEL = 0.04f;

// @TODO add (back) anisotropy
// @TODO refactor these functions into proper brdf calculations without dups

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

float diffuse_btdf()
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

void shade_cook_torrance(
  vec3 n, vec3 l, vec3 v, float metalness, float roughness, vec3 albedo, float transmission, vec3 transCol,
  out vec3 diff, out vec3 spec)
{
  vec3 c = albedo;

  vec3 c_diff = mix(c, vec3(0.0f), metalness);

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

  if (nv < SHADER_EPSILON)
  {
    diff = spec = vec3(0.f);
    return;
  }

  bool isTrasnmissive = transmission > SHADER_EPSILON; 

  if (!isTrasnmissive && nl < SHADER_EPSILON)
  {
    diff = spec = vec3(0.f);
    return;
  }

  float inlu = -dot(nn, ll);

  float a = roughness * roughness;
  float a2 = a * a;

  vec3 f0 = mix(vec3(F_DIEL), c, metalness);
  vec3 f = conductor_frensel_shlick(f0, hv);

  vec3 spec_bsdf = vec3(nl * specular_brdf(nl, nv, hl, hv, nh, a2));
  vec3 diff_bsdf;
  if (isTrasnmissive)
  {
    float wrap = mix(0.f, 0.5f, luminance_bt601(transCol));
    float wrapNormalizationFactor = 1.f / ((1.f + wrap) * (1.f + wrap));
    float frontFactor = max((nlu + wrap) * wrapNormalizationFactor, 0.f);
    float backFactor = max((inlu + wrap) * wrapNormalizationFactor, 0.f);
    diff_bsdf = mix(vec3(frontFactor * diffuse_brdf()), backFactor * diffuse_btdf() * transCol, transmission);
  }
  else
  {
    diff_bsdf = vec3(nl * diffuse_brdf());
  }

  diff = (1.f - f) * diff_bsdf * c_diff;
  spec = f * spec_bsdf;
}

void shade_cook_torrance_diffuse_spec_gloss(
  vec3 n, vec3 l, vec3 v, vec3 diffuse, vec3 specular, float glossiness, float transmission, vec3 transCol,
  out vec3 diff, out vec3 spec)
{
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

  if (nv < SHADER_EPSILON)
  {
    diff = spec = vec3(0.f);
    return;
  }

  bool isTrasnmissive = transmission > SHADER_EPSILON; 

  if (!isTrasnmissive && nl < SHADER_EPSILON)
  {
    diff = spec = vec3(0.f);
    return;
  }

  float inlu = -dot(nn, ll);

  float roughness = 1.f - glossiness;
  float a = roughness * roughness;
  float a2 = a * a;

  vec3 f0 = specular;

  vec3 c_diff = diffuse * (1.f - max(specular.r, max(specular.g, specular.b)));

  vec3 f = conductor_frensel_shlick(f0, hv);

  vec3 spec_bsdf = vec3(nl * specular_brdf(nl, nv, hl, hv, nh, a2));
  vec3 diff_bsdf;
  if (isTrasnmissive)
  {
    float wrap = mix(0.f, 0.5f, luminance_bt601(transCol));
    float wrapNormalizationFactor = 1.f / ((1.f + wrap) * (1.f + wrap));
    float frontFactor = max((nlu + wrap) * wrapNormalizationFactor, 0.f);
    float backFactor = max((inlu + wrap) * wrapNormalizationFactor, 0.f);
    diff_bsdf = mix(vec3(frontFactor * diffuse_brdf()), backFactor * diffuse_btdf() * transCol, transmission);
  }
  else
  {
    diff_bsdf = vec3(nl * diffuse_brdf());
  }

  diff = (1.f - f) * diff_bsdf * c_diff;
  spec = f * spec_bsdf;
}

void calculate_pbr(
  vec3 normal, vec3 lightDir, vec3 viewVec,
  float metalness, float roughness, vec3 albedo, float transmission, vec3 transCol,
  out vec3 diff, out vec3 spec)
{
  shade_cook_torrance(
    normal, lightDir, viewVec, metalness, roughness, albedo, transmission, transCol,
    diff, spec);
}

void calculate_pbr_diff_spec_gloss(
  vec3 normal, vec3 lightDir, vec3 viewVec,
  vec3 diffuse, vec3 specular, float glossiness, float transmission, vec3 transCol,
  out vec3 diff, out vec3 spec)
{
  shade_cook_torrance_diffuse_spec_gloss(
    normal, lightDir, viewVec, diffuse, specular, glossiness, transmission, transCol,
    diff, spec);
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
  int cascade = 0;
  float zIntoCascadeStart = 0.f;
  float zIntoCascadeEnd = 0.f;
  float zCascadeSize = 0.f;
  float prevSplit = 0.f;
  float split = viewParams.viewFrustum.nearZ;
  while (cascade < CSM_CASCADE_COUNT)
  {
    zIntoCascadeStart = viewPos.z - split;
    split = get_frustum_split(viewParams, cascade);
    zIntoCascadeEnd = split - viewPos.z;
    zCascadeSize = split - prevSplit;

    if (viewPos.z <= split)
      break;

    prevSplit = split;

    ++cascade;
  }

  float zNextCascadeSize = zCascadeSize;
  if (cascade < CSM_CASCADE_COUNT - 1)
    zNextCascadeSize = get_frustum_split(viewParams, cascade + 1) - split;

  if (constants.drawCascadesInSolidColor != 0)
  {
    const vec3 DEBUG_CASCADE_COLORS[4] = {
      vec3(1.f, 0.f, 0.f), vec3(0.f, 1.f, 0.f), vec3(0.f, 0.f, 1.f), vec3(0.f, 1.f, 1.f)};

    debugMultiplier = cascade == CSM_CASCADE_COUNT
      ? vec4(1.f, 0.f, 1.f, 1.f)
      : vec4(DEBUG_CASCADE_COLORS[cascade & 3], 1.f);
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

    if (constants.useDirectionalLightShadows != 0 && cascade < CSM_CASCADE_COUNT)
    {
      if (SHADOW_TECHNIQUE_IS_PCF(constants.directionalLightShadowsTechnique))
      {
        shadow = calculate_csm_shadow_pcf(
          i, cascade, pos, zIntoCascadeStart / zCascadeSize, zIntoCascadeEnd / zNextCascadeSize);
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
