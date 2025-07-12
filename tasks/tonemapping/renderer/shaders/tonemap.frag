#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "constants.h"
#include "tonemapping.h"


layout(location = 0) out vec4 out_fragColor;

layout(binding = 0) uniform sampler2D hdrImage;
layout(binding = 1, set = 0) readonly buffer minmax_t
{
  HistogramData histData;
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
  const vec4 hdrColor = textureLod(hdrImage, surf.texCoord, 0.f);

  vec3 ldrColor;

  if (constants.useTonemapping != 0)
  {
    const float minLuminance = normalized_to_luminance(histData.minNormLuminance);
    const float maxLuminance = normalized_to_luminance(histData.maxNormLuminance);
    const float minLogLuminance = to_log10(minLuminance);
    const float maxLogLuminance = to_log10(maxLuminance);

    const float worldLuminance = luminance_bt601(hdrColor);
    const float worldLogLuminance = to_log10(worldLuminance);

    const uint bin = uint(floor(
      float((worldLogLuminance - minLogLuminance) / (maxLogLuminance - minLogLuminance)) *
      float(HISTOGRAM_BINS)));
    const float P = histData.binsDistibution[clamp(bin, 0, HISTOGRAM_BINS - 1)];

    const float displayLogLuminance = minLogLuminance + (maxLogLuminance - minLogLuminance) * P;
    const float displayLuminance = from_log10(displayLogLuminance);

    ldrColor = hdrColor.xyz * displayLuminance / worldLuminance;
  }
  else
  {
    ldrColor = clamp(hdrColor.xyz, 0.f, 1.f);
  }

  out_fragColor = vec4(ldrColor, 1.f);
}
