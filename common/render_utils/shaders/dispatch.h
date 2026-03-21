#ifndef DISPATCH_H_INCLUDED
#define DISPATCH_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "defs.h"

struct IndirectDispatchCommand
{
  shader_uint wgX;
  shader_uint wgY;
  shader_uint wgZ;
};

shader_inline shader_uint get_linear_wg_count_disp(shader_uint work_count, shader_uint wg_size)
{
  return (work_count - 1) / wg_size + 1;
}

#endif // DISPATCH_H_INCLUDED
