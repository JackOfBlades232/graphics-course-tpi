#ifndef DRAW_H_INCLUDED
#define DRAW_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "materials.h"
#include "defs.h"

#define TERRAIN_CHUNK_INSTANCE_FLAG 0x1

struct IndirectCommand
{
  shader_uint indexCount;
  shader_uint instanceCount;
  shader_uint firstIndex;
  shader_int vertexOffset;
  shader_uint firstInstance;
};

struct CullableInstance
{
  shader_uint instId;                                                                                          
  shader_uint materialId;
  shader_uint commandId;
  shader_uint flags;
};

struct DrawableInstance
{
  shader_uint instId;                                                                                          
  shader_uint materialId;
  shader_uint flags;
};

#ifdef __cplusplus

inline uint32_t get_linear_wg_count(uint32_t work_count, uint32_t wg_size)
{
  return (work_count - 1) / wg_size + 1;
}

#endif

#endif // DRAW_H_INCLUDED




