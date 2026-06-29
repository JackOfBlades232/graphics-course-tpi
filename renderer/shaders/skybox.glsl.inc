#ifndef SKYBOXINC_H_INCLUDED
#define SKYBOXINC_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "skybox.h"

vec3 get_envi_ambient_from_skybox(in SkyboxSourceData sb)
{
  vec3 ambientCol = vec3(1.f);
  if (constants.useSkybox != 0 && constants.useSkyboxForAmbient != 0)
  {
    float maxLod = floor(log2(bindless_tex_cube_size(sb.cubemapTexSmp).x));
    ambientCol *= sample_bindless_tex_cube_lod(sb.cubemapTexSmp, vec3(0.f, 1.f, 0.f), maxLod).xyz;
  }
  return ambientCol;
}

#endif // SKYBOXINC_H_INCLUDED

