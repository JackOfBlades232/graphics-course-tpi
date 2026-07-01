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

vec3 sample_sky_light(vec3 dir, in SkyboxSourceData sb)
{
  vec3 skyColor = vec3(0.f);
  vec3 sunColor = vec3(0.f);
  if (constants.useSkybox != 0)
  {
    skyColor = sample_bindless_tex_cube(skybox.cubemapTexSmp, dir).xyz;
  }
  if (lights.directionalLightsCount > 0)
  {
    vec3 dirToSun = -normalize(lights.directionalLights[0].direction);
    vec3 sunCol = lights.directionalLights[0].intensity * lights.directionalLights[0].color;
    float cosToSky = clamp(dot(dir, dirToSun), 0.f, 1.f);
    sunColor = sunCol * pow(cosToSky, 3500.f);
  }
  return skyColor + sunColor;
}

#endif // SKYBOXINC_H_INCLUDED

