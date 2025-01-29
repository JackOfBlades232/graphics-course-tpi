#ifndef LIGHTS_H_INCLUDED
#define LIGHTS_H_INCLUDED

#include "cpp_glsl_compat.h"

// @TODO: compress attributes
// @TODO: should this be moved somewhere logically?

struct PointLight
{
  shader_vec3 position;
  float range;
  shader_vec3 color;
  float intensity;
};

struct SpotLight
{
  shader_vec3 position;
  float pad0;
  shader_vec3 direction;
  float range;
  shader_vec3 color;
  float intensity;
  float innerConeAngle;
  float outerConeAngle;
  shader_vec2 pad1;
};

struct DirectionalLight
{
  shader_vec3 direction;
  float pad0;
  shader_vec3 color;
  float intensity;
};

#define POINT_LIGHT_BUF_SIZE 256
#define SPOT_LIGHT_BUF_SIZE 64
#define DIRECTIONAL_LIGHT_BUF_SIZE 32

struct UniformLights
{
  PointLight pointLights[POINT_LIGHT_BUF_SIZE];
  SpotLight spotLights[SPOT_LIGHT_BUF_SIZE];
  DirectionalLight directionalLights[DIRECTIONAL_LIGHT_BUF_SIZE];
  shader_uint pointLightsCount;
  shader_uint spotLightsCount;
  shader_uint directionalLightsCount;
  shader_uint pad0;
};

#endif // LIGHTS_H_INCLUDED
