#ifndef TONEMAP_H_INCLUDED
#define TONEMAP_H_INCLUDED

#include "cpp_glsl_compat.h"

shader_vec3 gamma_encode(shader_vec3 ldr)
{
  return shader_pow(ldr, shader_vec3(1.f/2.2f));
}

#define GAMMA_ENCODE_COND(ldr_, cond_) ((cond_) ? gamma_encode(ldr_) : (ldr_))

#endif // TONEMAP_H_INCLUDED

