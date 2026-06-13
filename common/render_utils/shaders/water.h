#ifndef WATER_H_INCLUDED
#define WATER_H_INCLUDED

#include "cpp_glsl_compat.h"

#define WATER_CASCADE_COUNT 3
#define WATER_CASCADE_RES 256

struct WaterSourceData
{
  float waterLevel;
  float pad1_, pad2_, pad3_;
};

#endif // WATER_H_INCLUDED

