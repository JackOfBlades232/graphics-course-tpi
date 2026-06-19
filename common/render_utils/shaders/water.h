#ifndef WATER_H_INCLUDED
#define WATER_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "defs.h"

#define WATER_CASCADE_COUNT 3
#define WATER_CASCADE_RES 256

#define WATER_WORKGROUP_DIM 16

struct WaterSourceData
{
  float waterLevel;
  float l0;
  float l1;
  float l2;
};

#endif // WATER_H_INCLUDED

