#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "histogram_tonemapping.h"
#include "constants.h"

#include "common.frag.inc"


layout(location = 0) out vec4 out_fragColor;

layout(binding = 0) uniform sampler2D hdrImage;
layout(binding = 1, set = 0) readonly buffer hist_t
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

float get_bin_dist(int bin)
{
  return bin < 0 ? 0.f : (bin >= HISTOGRAM_BINS ? 1.f : histData.binsDistibution[bin]);
}

void main(void)
{
  const vec4 hdrColor = textureLod(hdrImage, surf.texCoord, 0.f);

  vec3 ldrColor;

  if (constants.useTonemapping != 0)
  {
    const float worldLuminance = luminance_bt601(hdrColor);
    const float worldLogLuminance = to_logscale(worldLuminance);

    const float fbinRaw =
      float((worldLogLuminance - histData.minLogLuminance) /
        (histData.maxLogLuminance - histData.minLogLuminance)) *
      float(HISTOGRAM_BINS);
    const float fbin = clamp(fbinRaw, 0.f, float(HISTOGRAM_BINS) - SHADER_EPSILON);

    const float inbin = fract(fbin);

    const int lbin = int(inbin >= 0.5f ? floor(fbin) : floor(fbin) - 1);
    const int llbin = lbin - 1;
    const int rbin = lbin + 1;
    const int rrbin = lbin + 2;

    const float t = inbin >= 0.5f ? inbin - 0.5f : inbin + 0.5f;
    float P = dot(
      catmull_rom_cubic_coeffs(t),
      vec4(get_bin_dist(llbin), get_bin_dist(lbin), get_bin_dist(rbin), get_bin_dist(rrbin)));

    const float displayLogLuminance =
      MIN_DISPLAY_LOG_LUMINANCE +
      (MAX_DISPLAY_LOG_LUMINANCE - MIN_DISPLAY_LOG_LUMINANCE) * P;
    const float displayLuminance = from_logscale(displayLogLuminance);

    ldrColor = hdrColor.xyz * displayLuminance / worldLuminance;
  }
  else
  {
    ldrColor = clamp(hdrColor.xyz, 0.f, 1.f);
  }

  out_fragColor = vec4(pow(ldrColor, vec3(1.f/2.2f)), 1.f);
}
