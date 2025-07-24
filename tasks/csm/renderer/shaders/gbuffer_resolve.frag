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

layout(binding = 2, set = 0) uniform sampler2D gbufAlbedo;
layout(binding = 3, set = 0) uniform sampler2D gbufMaterial;
layout(binding = 4, set = 0) uniform sampler2D gbufNormal;

layout(binding = 5, set = 0) uniform sampler2D gbufDepth;

layout(binding = 7, set = 0) uniform skybox_t
{
  SkyboxSourceData skybox;
};
layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

#include "bindless.glsl.inc"

layout(location = 0) in VS_OUT
{
  vec2 texCoord;
} surf;

vec3 depth_and_tc_to_pos(float depth, vec2 tc)
{
  const vec4 cameraToScreen = vec4(2.f * tc - 1.f, depth, 1.f); 
  const vec4 posHom = inverse(constants.mProjView) * cameraToScreen;
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

float calculate_angular_attenuation(float cosine, float lightAngleScale, float lightAngleOffset)
{
  float angularAttenuation = clamp(cosine * lightAngleScale + lightAngleOffset, 0.f, 1.f);
  angularAttenuation *= angularAttenuation;
  return angularAttenuation;
}

void main(void)
{
  // Unpack gbuffer
  const float depth = texture(gbufDepth, surf.texCoord).x;

  const mat4 invView = inverse(constants.mView);
  const vec3 camPos = invView[3].xyz / invView[3].w;

  const vec3 pos = depth_and_tc_to_pos(depth, surf.texCoord);
  const vec3 viewVec = normalize(camPos - pos);

  if (depth > 0.999999f)
  {
    if (constants.useSkybox == 0)
      out_fragColor = vec4(0.f, 0.f, 0.f, 1.f);
    else
      out_fragColor = sample_bindless_tex_cube(skybox.cubemapTexSmp, -vec3(viewVec));
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

  for (uint i = 0; i < lights.directionalLightsCount; ++i)
  {
    const vec3 lightIntensity = 
      lights.directionalLights[i].color * lights.directionalLights[i].intensity;
    const vec3 lightDir = -normalize(lights.directionalLights[i].direction);

    if (mat == MATERIAL_PBR)
      color += calculate_pbr(normal, lightDir, viewVec, matData.y, matData.z, albedo, lightIntensity);
    else if (mat == MATERIAL_DIFFUSE)
      color += calculate_diffuse(normal, lightDir, albedo, lightIntensity);
  }

  for (uint i = 0; i < lights.pointLightsCount; ++i)
  {
    const vec3 lightIntensity = lights.pointLights[i].color * lights.pointLights[i].intensity;
    const float lightAttenuation =
      calculate_attenuation(pos, lights.pointLights[i].position, lights.pointLights[i].range);
    const vec3 lightDir = normalize(lights.pointLights[i].position - pos);
    const vec3 lightColor = lightIntensity * lightAttenuation;

    if (mat == MATERIAL_PBR)
      color += calculate_pbr(normal, lightDir, viewVec, matData.y, matData.z, albedo, lightColor);
    else if (mat == MATERIAL_DIFFUSE)
      color += calculate_diffuse(normal, lightDir, albedo, lightColor);
  }

  for (uint i = 0; i < lights.spotLightsCount; ++i)
  {
    const vec3 lightIntensity = lights.spotLights[i].color * lights.spotLights[i].intensity;
    const float lightAttenuation =
      calculate_attenuation(pos, lights.spotLights[i].position, lights.spotLights[i].range);

    const vec3 lightDir = normalize(lights.spotLights[i].direction);
    const vec3 fromPosDir = normalize(lights.spotLights[i].position - pos);

    // @TODO: this can be precalculated on cpu, left here to have a dumb imgui setting
    const float lightAngleScale =
      1.f / max(0.001f, cos(lights.spotLights[i].innerConeAngle) - cos(lights.spotLights[i].outerConeAngle));
    const float lightAngleOffset = -cos(lights.spotLights[i].outerConeAngle) * lightAngleScale;

    const float lightAngularAttenuation = calculate_angular_attenuation(
      dot(lightDir, -fromPosDir),
      lightAngleScale,
      lightAngleOffset);

    const vec3 lightColor = lightIntensity * lightAttenuation * lightAngularAttenuation;

    if (mat == MATERIAL_PBR)
      color += calculate_pbr(normal, fromPosDir, viewVec, matData.y, matData.z, albedo, lightColor);
    else if (mat == MATERIAL_DIFFUSE)
      color += calculate_diffuse(normal, fromPosDir, albedo, lightColor);
  }

  out_fragColor = vec4(color, 1.0f);
}
