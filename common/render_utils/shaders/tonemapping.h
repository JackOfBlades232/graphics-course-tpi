#ifndef TONEMAPPING_H_INCLUDED
#define TONEMAPPING_H_INCLUDED

#include "defs.h"
#include "cpp_glsl_compat.h"

#define HISTOGRAM_MINMAX_WORK_GROUP_SIZE (MAX_WORK_GROUP_SIZE / 8)
#define PIXELS_PER_THREAD 128

struct HistogramLuminanceRange
{
  uint min, max; // normalized at [0, 1] -> [0, 255]
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

#endif // TONEMAPPING_H_INCLUDED

