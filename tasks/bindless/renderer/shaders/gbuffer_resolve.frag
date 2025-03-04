#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "lights.h"
#include "materials.h"


layout(location = 0) out vec4 out_fragColor;

layout(push_constant) uniform params_t
{
  mat4 mProj;
  mat4 mView;
} params;

layout(binding = 1, set = 0) uniform light_data_t
{
  UniformLights lights;
};

layout(binding = 0, set = 1) uniform sampler2D gbufAlbedo;
layout(binding = 1, set = 1) uniform sampler2D gbufMaterial;
layout(binding = 2, set = 1) uniform sampler2D gbufNormal;

layout(binding = 8, set = 1) uniform sampler2D gbufDepth;

layout(location = 0) in VS_OUT
{
  vec2 texCoord;
} surf;

vec3 depth_and_tc_to_pos(float depth, vec2 tc, mat4 invProjView)
{
  const vec4 cameraToScreen = vec4(2.f * tc - 1.f, depth, 1.f); 
  const vec4 posHom = invProjView * cameraToScreen;
  return posHom.xyz / posHom.w;
}

const float EPS = 1e-5f;
const float PI = 3.14159265359f;
const float GAMMA_POW = 2.2f;
const float F_DIEL = 0.04f;

// @TODO add (back) anisotropy

#define HPLUS(v_) ((v_) > 0.f ? 1.f : 0.f)

float d_ggx(vec3 n, vec3 h, float alpha2)
{
  float nh = dot(n, h);
  float nh2 = nh * nh;
  float divterm = nh2 * (alpha2 - 1.f) + 1.f;
  return (alpha2 * HPLUS(nh)) / (PI * divterm * divterm);
}

float g_smith_ggx(vec3 n, vec3 l, vec3 v, vec3 h, float alpha2)
{
  float nl = dot(n, l);
  float nv = dot(n, v);
  float hl = dot(h, l);
  float hv = dot(h, v);

  float anl = abs(nl);
  float anv = abs(nv);

  float sl = sqrt(mix(nl * nl, 1.f, alpha2));
  float sv = sqrt(mix(nv * nv, 1.f, alpha2));

  return (4.f * anl * anv * HPLUS(hl) * HPLUS(hv)) / ((anl + sl) * (anv + sv));
}

vec4 shade_cook_torrance(
  vec3 n, vec3 l, vec3 v, float metalness, float roughness, vec3 albedo, vec3 lightCol)
{
  //vec3 c = albedo;
  vec3 c = pow(albedo, vec3(GAMMA_POW));
  //vec3 lc = lightCol;
  vec3 lc = pow(lightCol, vec3(GAMMA_POW));

  vec3 h = normalize(l + v);
  float alpha = roughness * roughness;
  float alpha2 = alpha * alpha;
  float ahv = abs(dot(h, v));
  float anl = abs(dot(n, l));
  float anv = abs(dot(n, v));
  float mixc = pow(1.f - ahv, 5.f);

  const vec3 BLACK = vec3(0.f);
  vec3 c_diff = mix(c, BLACK, metalness);
  vec3 f0 = mix(vec3(F_DIEL), c, metalness);

  vec3 f = f0 + (1.f - f0) * mixc;
  float d = d_ggx(n, h, alpha2);
  float g = g_smith_ggx(n, l, v, h, alpha2);

  vec3 f_diffuse = (1.f - f) * (1.f / PI) * c_diff;
  vec3 f_specular = f * d * g / (4.f * anv * anv);

  vec3 material = f_diffuse + f_specular;

  //return vec4(material * lc, 1.f); 
  return pow(vec4(material * lc, 1.f), 1.f / vec4(GAMMA_POW)); 
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

void main()
{
  // Unpack gbuffer
  const float depth = texture(gbufDepth, surf.texCoord).x;
  if (depth > 0.9999f)
  {
    out_fragColor = vec4(vec3(0.f), 1.f);
    return;
  }

  const mat4 invProjView = inverse(params.mProj * params.mView);
  const vec3 camPos = params.mView[3].xyz / params.mView[3].w;

  const vec3 albedo = texture(gbufAlbedo, surf.texCoord).xyz;
  const vec4 matData = texture(gbufMaterial, surf.texCoord);
  const vec3 normal = texture(gbufNormal, surf.texCoord).xyz;

  const vec3 pos = depth_and_tc_to_pos(depth, surf.texCoord, invProjView);
  const vec3 viewVec = normalize(camPos - pos);

  const uint mat = uint(matData.x); 

  if (mat != MATERIAL_PBR && mat != MATERIAL_DIFFUSE)
  {
    out_fragColor = vec4(1.f, 0.f, 1.f, 1.f);
    return;
  }
  
  // Calculate lighting
  
  // @TODO: parametrize
  const float ambient = 0.01f;

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
