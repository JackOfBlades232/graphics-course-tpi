#ifndef LIGHTS_H_INCLUDED
#define LIGHTS_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "materials.h"

// @TODO: compress attributes
// @TODO: should this be moved somewhere logically?

#define POINT_LIGHT_BUF_SIZE 256
#define SPOT_LIGHT_BUF_SIZE 64
#define DIRECTIONAL_LIGHT_BUF_SIZE 1

#define CSM_CASCADE_COUNT 8
#define CSM_CASCADE_RESOLUTION 1024
#define POINT_SM_RESOLUTION 512
#define SPOT_SM_RESOLUTION 512

struct CascadeData
{
  TexSmpIdPair map;
  shader_uint pad1_, pad2_, pad3_;
};

struct PointLight
{
  shader_vec3 position;
  float range;
  shader_vec3 color;
  float intensity;
  TexSmpIdPair shadowmap;
  float pad0_, pad1_, pad2_;
};

struct SpotLight
{
  shader_vec3 position;
  TexSmpIdPair shadowmap;
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
  CascadeData shadowmapCascades[CSM_CASCADE_COUNT];
};

struct UniformLights
{
  PointLight pointLights[POINT_LIGHT_BUF_SIZE];
  SpotLight spotLights[SPOT_LIGHT_BUF_SIZE];
  DirectionalLight directionalLights[DIRECTIONAL_LIGHT_BUF_SIZE];
  shader_uint pointLightsCount;
  shader_uint spotLightsCount;
  shader_uint directionalLightsCount;
  shader_uint pad_;
};

struct LightMatrices
{
  shader_mat4 pointLightMats[POINT_LIGHT_BUF_SIZE][6];
  shader_mat4 spotLightMats[SPOT_LIGHT_BUF_SIZE];
  shader_mat4 directionalLightMats[DIRECTIONAL_LIGHT_BUF_SIZE][CSM_CASCADE_COUNT];
};

#endif // LIGHTS_H_INCLUDED
