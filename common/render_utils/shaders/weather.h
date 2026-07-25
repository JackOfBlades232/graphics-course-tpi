#ifndef WIND_H_INCLUDED
#define WIND_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "defs.h"

struct WeatherSourceData
{
  shader_vec2 windDirection;
  float windStrength;
  float fogRho0;
  float fogHBase;
  float fogHC;
  float fogShapeRngMin;
  float fogShapeRngMax;
  float fogShapeScale;
  float fogShapeWindInfluence;
  float fogInscatterC;
  float pad_;
};

#endif // WIND_H_INCLUDED

