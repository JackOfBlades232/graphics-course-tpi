#ifndef TEMPORAL_HELPERS_H_INCLUDED
#define TEMPORAL_HELPERS_H_INCLUDED

#include "geometry.h"

vec2 unjitter_surface_texcoord(vec2 tc, in ViewParams params)
{
  return tc - params.subpixelUvJitterX * dFdx(tc) - params.subpixelUvJitterY * dFdy(tc);
}

#endif
