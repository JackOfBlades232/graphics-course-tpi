#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "constants.h"

layout(location = 0) out vec4 out_fragColor;

layout(binding = 0) uniform sampler2D hdrImage;

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

// @TODO: read up on ACES

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
vec3 ACESFilm(vec3 x)
{
  float a = 2.51f;
  float b = 0.03f;
  float c = 2.43f;
  float d = 0.59f;
  float e = 0.14f;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.f, 1.f);
}

void main(void)
{
  const vec4 hdrColor = textureLod(hdrImage, surf.texCoord, 0.f);

  vec3 ldrColor;

  if (constants.useTonemapping != 0)
    ldrColor = ACESFilm(hdrColor.xyz * constants.acesExposure);
  else
    ldrColor = clamp(hdrColor.xyz, 0.f, 1.f);

  out_fragColor = vec4(pow(ldrColor, vec3(1.f/2.2f)), 1.f);
}

