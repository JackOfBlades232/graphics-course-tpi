#ifndef CONSTANTS_H_INCLUDED
#define CONSTANTS_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "geometry.h"

struct Constants
{
  shader_mat4 mProjView;
  shader_mat4 mView;
  ViewFrustum viewFrustum;
  CullingMode cullingMode;
};

#endif // CONSTANTS_H_INCLUDED

