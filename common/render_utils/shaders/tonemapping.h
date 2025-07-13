#ifndef TONEMAPPING_H_INCLUDED
#define TONEMAPPING_H_INCLUDED

#include "defs.h"
#include "cpp_glsl_compat.h"

#define HISTOGRAM_WORK_GROUP_SIZE (MAX_WORK_GROUP_SIZE / 8)
#define HISTOGRAM_PIXELS_PER_THREAD 128
#define HISTOGRAM_BINS 100

struct HistogramData
{
  uint minNormLuminance, maxNormLuminance; // normalized at [0, 1] -> [0, 255]
  float minLuminance, maxLuminance;
  float minLogLuminance, maxLogLuminance;
  uint binsDensity[HISTOGRAM_BINS];
  float binsDistibution[HISTOGRAM_BINS];
};

struct HistogramLuminanceRange
{
  uint min, max; 
};

float luminance_bt601(shader_vec4 col)
{
  return 0.299f * col.r + 0.587f * col.g + 0.114f * col.b;
}

shader_uint luminance_to_normalized(float luminance)
{
  // @TODO: maybe floor(*256)?
  return shader_uint(luminance * 255.f);
}

float normalized_to_luminance(shader_uint normalized)
{
  return float(normalized) / 255.f;
}

float to_log10(float lin)
{
  return (1.f / shader_log(10.f)) * shader_log(lin + 1.f);
}

float from_log10(float loga)
{
  return shader_exp(loga * shader_log(10.f)) - 1.f;
}

#endif // TONEMAPPING_H_INCLUDED

