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
  CullingMode cullingMode;

  shader_uint pad1_;
  shader_uint pad2_;
  shader_uint pad3_;
};

#endif // CONSTANTS_H_INCLUDED

