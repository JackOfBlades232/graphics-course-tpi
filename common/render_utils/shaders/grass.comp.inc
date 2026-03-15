#ifndef GRASS_H_INCLUDED
#define GRASS_H_INCLUDED

#include "terrain.h"

#define GRASS_GEN_DATA_MEMBERS \
  int chunkCount;              \
  int pad_;                    \
  shader_vec2 chunkOrigins[];

#endif
