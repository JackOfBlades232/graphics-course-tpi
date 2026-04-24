#ifndef SSAO_H_INCLUDED
#define SSAO_H_INCLUDED

#include "cpp_glsl_compat.h"

#define SSAO_KERNEL_MAX_SIZE 128
#define SSAO_BLUR_KERNEL_SIZE 5
#define SSAO_BLUR_KERNEL_HS (SSAO_BLUR_KERNEL_SIZE / 2)

#define SSAO_BLUR_KERNEL_TSIZE (SSAO_BLUR_KERNEL_SIZE * SSAO_BLUR_KERNEL_SIZE)
#define SSAO_BLUR_KERNEL_TSIZE_V4 ((SSAO_BLUR_KERNEL_TSIZE + (SSAO_BLUR_KERNEL_TSIZE & 1)) / 2)

struct SsaoConstData
{
  shader_vec4 ssaoKernel[SSAO_KERNEL_MAX_SIZE];
  shader_vec4 ssaoKernelRotations[SSAO_BLUR_KERNEL_TSIZE_V4];
  // @TODO: kernel rotations for less patterns
};

#endif
