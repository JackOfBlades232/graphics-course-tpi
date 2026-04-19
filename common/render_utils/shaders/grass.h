#ifndef GRASS_H_INCLUDED
#define GRASS_H_INCLUDED

#include "terrain.h"

#define GRASS_GEN_DATA_MEMBERS                                                                     \
  int chunkCount;                                                                                  \
  int pad_;                                                                                        \
  shader_vec2 chunkOrigins[];

#define GRASS_ID_BITS 3
#define GRASS_SF_BITS (32 - GRASS_ID_BITS)
#define GRASS_ID_MASK (((1 << GRASS_ID_BITS) - 1) << GRASS_SF_BITS)
#define GRASS_SF_MASK ((1 << GRASS_SF_BITS) - 1)

struct GrassInstance
{
  shader_vec3 pos;
  shader_uint idAndSf;
  shader_vec3 windOffset;
  shader_uint dirQuantized;
};

#define GRASS_INST_GET_ID(inst_) ((inst_).idAndSf >> GRASS_SF_BITS)
#define GRASS_INST_GET_SF(inst_)                                                                   \
  (uintBitsToFloat(((inst_).idAndSf & GRASS_SF_MASK) << (GRASS_ID_BITS - 1)))

#define GRASS_SANK_PORTION 0.15f

#endif
