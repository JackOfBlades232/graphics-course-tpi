#ifndef SSAO_H_INCLUDED
#define SSAO_H_INCLUDED

#include "cpp_glsl_compat.h"

#define SSAO_KERNEL_SIZE 64

struct SsaoConstData
{
  shader_vec4 ssaoKernel[SSAO_KERNEL_SIZE];
  // @TODO: kernel rotations for less patterns
};

#endif

