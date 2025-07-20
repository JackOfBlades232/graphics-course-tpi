#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "tonemapping.h"


layout(location = 0) out vec4 out_fragColor;

layout(binding = 0, set = 0) readonly buffer hist_t
{
  HistogramData data;
};

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

const vec4 BG_COLOR = vec4(0.1f, 0.1f, 0.1f, 0.5f);
const vec4 BIN_COLOR = vec4(0.529f, 0.808f, 0.922f, 1.f);

const float DIST_DRAW_PAD = 0.005f;

float get_bin_float_density(int bin)
{
  return bin == 0 ? data.binsDistibution[0] :
    data.binsDistibution[bin] - data.binsDistibution[bin - 1];
}

void main(void)
{
  const float yFill = 1.f - surf.texCoord.y;

  if (surf.texCoord.x <= 0.5f)
  {
    const float binf = floor(surf.texCoord.x * 2.f * float(HISTOGRAM_BINS));
    const uint bin = clamp(uint(binf), 0, HISTOGRAM_BINS - 1);

    float maxDensity = 0.f;
    for (int i = 0; i < HISTOGRAM_BINS; ++i)
      maxDensity = max(maxDensity, get_bin_float_density(i));

    const float binDensity = get_bin_float_density(int(bin));

    out_fragColor = (binDensity / maxDensity) >= yFill ? BIN_COLOR : BG_COLOR;
  }
  else
  {
    const float binf = floor((surf.texCoord.x - 0.5f) * 2.f * float(HISTOGRAM_BINS));
    const uint bin = clamp(uint(binf), 0, HISTOGRAM_BINS - 1);

    const float binDist = data.binsDistibution[bin];
    const float prevBinDist = bin == 0 ? 0.f : data.binsDistibution[bin - 1];
    out_fragColor =
      max(yFill - binDist, prevBinDist - yFill) <= DIST_DRAW_PAD ?
      BIN_COLOR : BG_COLOR;
  }
}
