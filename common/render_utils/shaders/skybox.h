#ifndef SKYBOX_H_INCLUDED
#define SKYBOX_H_INCLUDED

#include "materials.h"
#include "cpp_glsl_compat.h"

struct SkyboxSourceData
{
  TexSmpIdPair cubemapTexSmp;
  shader_uint pad1_, pad2_, pad3_;
};

#endif // SKYBOX_H_INCLUDED

