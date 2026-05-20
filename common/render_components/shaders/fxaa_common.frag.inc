#ifndef FXAA_COMMON_FRAG_H_INCLUDED
#define FXAA_COMMON_FRAG_H_INCLUDED

#include "tonemap.frag.inc"

float fxaa_luma(vec3 rgb)
{
  if (constants.fxaaAntialiasInSrgb)
    return rgb.y * (0.587f / 0.299f) + rgb.x;
  else
    return sqrt(dot(rgb, vec3(0.299f, 0.587f, 0.114f)))
}

float fxaa_high_pass_filter(float l, float c, float r)
{
  return 0.25f * l + 0.5f * c + 0.25f * r;
}

#endif // FXAA_COMMON_FRAG_H_INCLUDED


