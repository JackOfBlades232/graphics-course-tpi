#ifndef GEOMETRY_H_INCLUDED
#define GEOMETRY_H_INCLUDED

#include "cpp_glsl_compat.h"

struct BBox
{
  shader_vec4 min;
  shader_vec4 max;
};

struct OBBox
{
  shader_vec3 center;
  shader_vec3 extents;
  shader_vec3 xAxis;
  shader_vec3 yAxis;
  shader_vec3 zAxis;
};

struct ViewFrustum
{
  shader_float nearX;
  shader_float nearY;
  shader_float nearZ;
  shader_float farZ;
};

#ifdef __cplusplus

enum class CullingMode : shader_uint
{
  PER_VERTEX = 0,
  SAT = 1
};

#define CULLING_MODE_PER_VERTEX CullingMode::PER_VERTEX
#define CULLING_MODE_SAT CullingMode::SAT

#else

#define CullingMode shader_uint
#define CULLING_MODE_PER_VERTEX 0
#define CULLING_MODE_SAT 1

#endif

#endif // GEOMETRY_H_INCLUDED
