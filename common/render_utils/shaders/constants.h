#ifndef CONSTANTS_H_INCLUDED
#define CONSTANTS_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "geometry.h"

#define BIG_EPSILON 0.001f

struct Constants
{
  shader_mat4 mProjView;
  shader_mat4 mView;
  ViewFrustum viewFrustum;
  shader_vec3 playerWorldPos;
  CullingMode cullingMode;

  shader_vec3 toroidalOffset;

  shader_uint useTonemapping;

  shader_vec3 toroidalUpdatePlayerWorldPos;

  shader_uint pad2_;
};

#endif // CONSTANTS_H_INCLUDED

