#ifndef WATER_H_INCLUDED
#define WATER_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "defs.h"

#define WATER_CASCADE_COUNT 3
#define WATER_CASCADE_RES_POW 8
#define WATER_CASCADE_RES (1 << WATER_CASCADE_RES_POW)

#define WATER_WORKGROUP_DIM 16

// 2 buffers per cascade -- (x, y, z, dXZ), (dYX, dYZ, dXX, dZZ)
#define WATER_FFT_BUFFER_COUNT (2 * WATER_CASCADE_COUNT)

struct WaterSourceData
{
  float waterLevel;
  float l0;
  float l1;
  float l2;
};

#endif // WATER_H_INCLUDED

