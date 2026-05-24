#ifndef FXAA_COMMON_FRAG_H_INCLUDED
#define FXAA_COMMON_FRAG_H_INCLUDED

#include "tonemap.frag.inc"

float fxaa_luma(vec3 rgb)
{
  if (constants.fxaaAntialiasInSrgb != 0)
    return rgb.y * (0.587f / 0.299f) + rgb.x;
  else
    return sqrt(dot(rgb, vec3(0.299f, 0.587f, 0.114f)));
}

float fxaa_high_pass_filter(float l, float c, float r)
{
  return abs(0.25f * l - 0.5f * c + 0.25f * r);
}

float fxaa311_high_pass_filter(float center, float corners)
{
  return abs(-2.f * center + corners);
}

const float fxaa_edge_threshold_min = 1.f / 16.f;
const float fxaa_edge_threshold = 1.f / 8.f;
const uint fxaa_search_steps = 16;
const float fxaa_subpixel_trim = 0.5f;
const float fxaa_subpixel_trim_scale = 1.f / (1.f - fxaa_subpixel_trim);
const float fxaa_subpixel_cap = 0.75f;

const float fxaa311_edge_threshold_min = 0.0312f;
const float fxaa311_edge_threshold_max = 0.125f;
const uint fxaa311_search_steps = 11;
const float fxaa311_search_qual[fxaa311_search_steps] =
{
  1.f, 1.f, 1.f, 1.f, 1.5f, 2.f, 2.f, 2.f, 2.f, 4.f, 8.f
};
const float fxaa311_subpixel_qual = 0.75f;

#define DEBUG_CUTOFF_PIXELS 0
#define DEBUG_SOLID_COLOR 0
#define DEBUG_EDGE_DIRECTION 0
#define DEBUG_SUBPIXEL_W 0

#endif // FXAA_COMMON_FRAG_H_INCLUDED


