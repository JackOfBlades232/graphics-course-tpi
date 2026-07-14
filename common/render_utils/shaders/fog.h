#ifndef FOG_H_INCLUDED
#define FOG_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "defs.h"

#define FOG_WORKGROUP_DIM 16

struct FogSourceData
{
  shader_vec3 color; 
  float constantTransmittance;
};

#endif // FOG_H_INCLUDED

