#ifndef WIND_H_INCLUDED
#define WIND_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "defs.h"

struct WindSourceData
{
  shader_vec2 direction;
  float strength;
  float pad_;
};

#endif // WIND_H_INCLUDED

