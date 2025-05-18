#ifndef DRAW_H_INCLUDED
#define DRAW_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "materials.h"

// @TODO: tweak, maybe move to another file?
#define CULLING_WORK_GROUP_SIZE 64

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
  shader_uint matrixId;                                                                                          
  shader_uint materialId;
  shader_uint commandId;
};

struct DrawableInstance
{
  shader_uint matrixId;                                                                                          
  shader_uint materialId;
};

#endif // DRAW_H_INCLUDED




