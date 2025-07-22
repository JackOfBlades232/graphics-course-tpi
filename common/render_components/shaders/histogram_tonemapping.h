#ifndef TONEMAPPING_H_INCLUDED
#define TONEMAPPING_H_INCLUDED

#include "defs.h"
#include "cpp_glsl_compat.h"

#define HISTOGRAM_WORK_GROUP_SIZE (MAX_WORK_GROUP_SIZE / 8)
#define HISTOGRAM_PIXELS_PER_THREAD 128
#define HISTOGRAM_BINS 100

struct HistogramData
{
  shader_uint minNormLuminance, maxNormLuminance; // normalized at [0, 1] -> [0, 255]
  float minLuminance, maxLuminance;
  float minLogLuminance, maxLogLuminance;
  shader_uint binsDensity[HISTOGRAM_BINS];
  shader_uint binsCumDensity[HISTOGRAM_BINS];
  float binsRefinedJnds[HISTOGRAM_BINS];
  shader_uint binsRefinedBinCounts[HISTOGRAM_BINS];
  shader_uint binsRefinedBinCumCounts[HISTOGRAM_BINS];
  shader_uint binsRefinedDensity[HISTOGRAM_BINS];
  shader_uint binsCumRefinedDensity[HISTOGRAM_BINS];
  float binsDistibution[HISTOGRAM_BINS];
};

struct HistogramLuminanceRange
{
  shader_uint min, max; 
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

const float LOGSCALE_BASE = 10.f;

float to_logscale(float lin)
{
  return (1.f / shader_log(LOGSCALE_BASE)) * shader_log(lin + 1.f);
}

float from_logscale(float loga)
{
  return shader_exp(loga * shader_log(LOGSCALE_BASE)) - 1.f;
}

#define MIN_DISPLAY_LOG_LUMINANCE to_logscale(0.f)
#define MAX_DISPLAY_LOG_LUMINANCE to_logscale(1.f)

#endif // TONEMAPPING_H_INCLUDED

