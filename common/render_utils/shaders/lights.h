#ifndef LIGHTS_H_INCLUDED
#define LIGHTS_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "materials.h"

// @TODO: compress attributes
// @TODO: should this be moved somewhere logically?

#define POINT_LIGHT_BUF_SIZE 256
#define SPOT_LIGHT_BUF_SIZE 64
#define DIRECTIONAL_LIGHT_BUF_SIZE 1

#define CSM_CASCADE_COUNT 4
#define CSM_CASCADE_RESOLUTION 2048
#define POINT_SM_RESOLUTION 1024
#define SPOT_SM_RESOLUTION 1024

#define CSM_CORRIDOR_SIZE 100.f

struct CascadeData
{
  TexSmpIdPair map;
  float minX, maxX, minY, maxY, minZ, maxZ; // View plane space
  shader_uint pad1_;
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


// clang-format off
//
// from https://github.com/TheRealMJP/Shadows
shader_inline const float PCF3X3_KERNEL[3][3] =
{
  { 0.5,1.0,0.5, },
  { 1.0,1.0,1.0, },
  { 0.5,1.0,0.5, }
};

shader_inline const float PCF5X5_KERNEL[5][5] =
{
  { 0.0,0.5,1.0,0.5,0.0 },
  { 0.5,1.0,1.0,1.0,0.5 },
  { 1.0,1.0,1.0,1.0,1.0 },
  { 0.5,1.0,1.0,1.0,0.5 },
  { 0.0,0.5,1.0,0.5,0.0 }
};

shader_inline const float PCF7X7_KERNEL[7][7] =
{
  { 0.0,0.0,0.5,1.0,0.5,0.0,0.0 },
  { 0.0,1.0,1.0,1.0,1.0,1.0,0.0 },
  { 0.5,1.0,1.0,1.0,1.0,1.0,0.5 },
  { 1.0,1.0,1.0,1.0,1.0,1.0,1.0 },
  { 0.5,1.0,1.0,1.0,1.0,1.0,0.5 },
  { 0.0,1.0,1.0,1.0,1.0,1.0,0.0 },
  { 0.0,0.0,0.5,1.0,0.5,0.0,0.0 }
};

shader_inline const float PCF9X9_KERNEL[9][9] =
{
  { 0.0,0.0,0.0,0.5,1.0,0.5,0.0,0.0,0.0 },
  { 0.0,0.0,1.0,1.0,1.0,1.0,1.0,0.0,0.0 },
  { 0.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.0 },
  { 0.5,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.5 },
  { 1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 },
  { 0.5,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.5 },
  { 0.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.0 },
  { 0.0,0.0,1.0,1.0,1.0,1.0,1.0,0.0,0.0 },
  { 0.0,0.0,0.0,0.5,1.0,0.5,0.0,0.0,0.0 }
};

// clang-format on

shader_inline const int PCF_KERNEL_SIZES[] = {0, 3, 5, 7, 9};

#ifdef __cplusplus

enum class ShadowTechnique
{
  SIMPLE = 0,
  PCF3X3,
  PCF5X5,
  PCF7X7,
  PCF9X9,

  // @TODO: VSM

  COUNT
};

#define SHADOW_TECHNIQUE_SIMPLE ShadowTechnique::SIMPLE
#define SHADOW_TECHNIQUE_PCF3X3 ShadowTechnique::PCF3X3
#define SHADOW_TECHNIQUE_PCF5X5 ShadowTechnique::PCF5X5
#define SHADOW_TECHNIQUE_PCF7X7 ShadowTechnique::PCF7X7
#define SHADOW_TECHNIQUE_PCF9X9 ShadowTechnique::PCF9X9

#else

#define ShadowTechnique shader_uint
#define SHADOW_TECHNIQUE_SIMPLE 0
#define SHADOW_TECHNIQUE_PCF3X3 1
#define SHADOW_TECHNIQUE_PCF5X5 2
#define SHADOW_TECHNIQUE_PCF7X7 3
#define SHADOW_TECHNIQUE_PCF9X9 4

#endif

#define SHADOW_TECHNIQUE_IS_PCF(t_) ((t_) <= SHADOW_TECHNIQUE_PCF9X9)
#define SHADOW_TECHNIQUE_IS_PCF_KERNEL(t_)                                                         \
  (SHADOW_TECHNIQUE_IS_PCF(t_) && (t_) >= SHADOW_TECHNIQUE_PCF3X3)

shader_inline float pcf_kernel_weight(int x, int y, ShadowTechnique tech)
{
  switch (tech)
  {
  case SHADOW_TECHNIQUE_PCF3X3:
    return PCF3X3_KERNEL[y][x];
  case SHADOW_TECHNIQUE_PCF5X5:
    return PCF5X5_KERNEL[y][x];
  case SHADOW_TECHNIQUE_PCF7X7:
    return PCF7X7_KERNEL[y][x];
  case SHADOW_TECHNIQUE_PCF9X9:
    return PCF9X9_KERNEL[y][x];
  default:
    return 0.f;
  }
}

#endif // LIGHTS_H_INCLUDED
