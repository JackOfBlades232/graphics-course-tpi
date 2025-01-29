#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "lights.h"


layout(location = 0) out vec4 out_fragColor;

layout(push_constant) uniform params_t
{
  mat4 mProjView;
} params;

layout(binding = 1) uniform light_data_t
{
  UniformLights lights;
};

layout(binding = 0, set = 1) uniform sampler2D gbufAlbedo;
layout(binding = 1, set = 1) uniform sampler2D gbufNormal;

layout(binding = 8, set = 1) uniform sampler2D gbufDepth;

layout(location = 0) in VS_OUT
{
  vec2 texCoord;
} surf;

vec3 depth_and_tc_to_pos(float depth, vec2 tc)
{
  const vec4 cameraToScreen = vec4(2.f * tc - 1.f, depth, 1.f); 
  const vec4 posHom = inverse(params.mProjView) * cameraToScreen;
  return posHom.xyz / posHom.w;
}

vec3 calculate_diffuse(vec3 lightDir, vec3 lightIntensity, vec3 normal)
{
  return max(dot(normal, lightDir), 0.0f) * lightIntensity;
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

  const vec4 albedo = texture(gbufAlbedo, surf.texCoord);
  const vec3 normal = texture(gbufNormal, surf.texCoord).xyz;

  const vec3 pos = depth_and_tc_to_pos(depth, surf.texCoord);
  
  // Calculate lighting
  
  // @TODO: parametrize
  const float ambient = 0.01;

  vec3 pixelColor = vec3(ambient);

  for (uint i = 0; i < lights.directionalLightsCount; ++i)
  {
    const vec3 lightIntensity = 
      lights.directionalLights[i].color * lights.directionalLights[i].intensity;

    pixelColor += calculate_diffuse(
      normalize(lights.directionalLights[i].direction),
      lightIntensity,
      normal);
  }

  for (uint i = 0; i < lights.pointLightsCount; ++i)
  {
    const vec3 lightIntensity = lights.pointLights[i].color * lights.pointLights[i].intensity;
    const float lightAttenuation =
      calculate_attenuation(pos, lights.pointLights[i].position, lights.pointLights[i].range);

    pixelColor += calculate_diffuse(
      normalize(pos - lights.pointLights[i].position),
      lightIntensity * lightAttenuation,
      normal);
  }

  for (uint i = 0; i < lights.spotLightsCount; ++i)
  {
    const vec3 lightIntensity = lights.spotLights[i].color * lights.spotLights[i].intensity;
    const float lightAttenuation =
      calculate_attenuation(pos, lights.spotLights[i].position, lights.spotLights[i].range);

    const vec3 lightDir = normalize(lights.spotLights[i].direction);
    const vec3 toPosDir = normalize(pos - lights.spotLights[i].position);

    // @TODO: this can be precalculated on cpu, left here to have a dumb imgui setting
    const float lightAngleScale =
      1.f / max(0.001f, cos(lights.spotLights[i].innerConeAngle) - cos(lights.spotLights[i].outerConeAngle));
    const float lightAngleOffset = -cos(lights.spotLights[i].outerConeAngle) * lightAngleScale;

    const float lightAngularAttenuation = calculate_angular_attenuation(
      dot(lightDir, toPosDir),
      lightAngleScale,
      lightAngleOffset);

    pixelColor += calculate_diffuse(
      toPosDir,
      lightIntensity * lightAttenuation * lightAngularAttenuation,
      normal);
  }

  out_fragColor = vec4(pixelColor, 1.0f);
}
