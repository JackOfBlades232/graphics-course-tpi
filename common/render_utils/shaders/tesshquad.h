#ifndef TESSHQUAD_H_INCLUDED
#define TESSHQUAD_H_INCLUDED

#include "materials.h"
#include "cpp_glsl_compat.h"

struct TesshquadParams
{
  shader_uint firstLevelChunks;
  shader_uint otherLevelsChunks;
  shader_uint chunksLevelDim;
  shader_uint chunkTessellationFactor;
  shader_uint levelCount;
  shader_uint chunksInstBase;
};

#endif // TESSHQUAD_H_INCLUDED

