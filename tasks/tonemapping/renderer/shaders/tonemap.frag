#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "constants.h"
#include "tonemapping.h"


layout(location = 0) out vec4 out_fragColor;

layout(binding = 0) uniform sampler2D hdrImage;
layout(binding = 1, set = 0) readonly buffer minmax_t
{
  HistogramLuminanceRange lumRange;
};

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

void main(void)
{
  // @TODO: real tonemapping
  const vec4 hdrColor = textureLod(hdrImage, surf.texCoord, 0.f);

  vec3 ldrColor;

  if (constants.useTonemapping != 0)
  {
    const float minLuminance = normalized_to_luminance(lumRange.min);
    const float maxLuminance = normalized_to_luminance(lumRange.max);

    const float luminance = luminance_bt601(hdrColor);
    ldrColor = ((hdrColor - minLuminance) / (maxLuminance - minLuminance)).xyz;
  }
  else
  {
    ldrColor = clamp(hdrColor.xyz, 0.f, 1.f);
  }

  out_fragColor = vec4(ldrColor, 1.f);
}
