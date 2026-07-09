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

#define WATER_TESSHQUAD_LEVEL_COUNT 8
#define WATER_TESSHQUAD_EXTENT_STEP 10.f

#define WATER_TESSHQUAD_CHUNKS_LEVEL_DIM 4
#define WATER_TESSHQUAD_FIRST_LEVEL_CHUNKS                                                         \
  (WATER_TESSHQUAD_CHUNKS_LEVEL_DIM * WATER_TESSHQUAD_CHUNKS_LEVEL_DIM)
#define WATER_TESSHQUAD_OTHER_LEVELS_CHUNKS                                                        \
  (WATER_TESSHQUAD_FIRST_LEVEL_CHUNKS - (WATER_TESSHQUAD_FIRST_LEVEL_CHUNKS / 4))
#define WATER_TESSHQUAD_TOTAL_CHUNK_COUNT                                                          \
  (WATER_TESSHQUAD_FIRST_LEVEL_CHUNKS +                                                            \
   (WATER_CASCADE_COUNT - 1) * WATER_TESSHQUAD_OTHER_LEVELS_CHUNKS)

#define WATER_TESSHQUAD_CHUNK_TESSELLATION_FACTOR 64

#define WATER_CAUSTIC_MAP_RES 1024

struct WaterSourceData
{
  float waterLevel;
  float l0;
  float l1;
  float l2;
};

#endif // WATER_H_INCLUDED
