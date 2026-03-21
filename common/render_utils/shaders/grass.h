#ifndef GRASS_H_INCLUDED
#define GRASS_H_INCLUDED

#include "terrain.h"

#define GRASS_GEN_DATA_MEMBERS                                                                     \
  int chunkCount;                                                                                  \
  int pad_;                                                                                        \
  shader_vec2 chunkOrigins[];

#define GRASS_ANGLE_BITS 9
#define GRASS_ID_BITS (32 - GRASS_ANGLE_BITS)
#define GRASS_ANGLE_MASK (((1 << GRASS_ANGLE_BITS) - 1) << GRASS_ID_BITS)
#define GRASS_ID_MASK ((1 << GRASS_ID_BITS) - 1)

struct GrassInstance
{
  shader_vec3 pos;
  shader_uint idAndAnglePacked;
};

#define GRASS_INSTANCE_ANGLE(inst_)                                                                \
  (float((inst_).idAndAnglePacked >> GRASS_ID_BITS) * SHADER_PI / 180.f)
#define GRASS_INSTANCE_ID(inst_) ((inst_).idAndAnglePacked & GRASS_ID_MASK)

#define GRASS_SANK_PORTION 0.2f

#endif
