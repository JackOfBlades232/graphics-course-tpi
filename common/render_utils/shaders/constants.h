#ifndef CONSTANTS_H_INCLUDED
#define CONSTANTS_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "geometry.h"
#include "lights.h"

#define BIG_EPSILON 0.001f

struct Constants
{
  shader_vec3 playerWorldPos;
  CullingMode cullingMode;

  shader_vec2 toroidalOffset;
  shader_vec2 toroidalUpdatePlayerWorldPos;

  float terrainNoiseRelHeightAmp;
  float terrainNoisePeriod;

  shader_uint drawTerrainSplattedDetail;

  shader_uint useSkybox;

  shader_uint useTonemapping;
  shader_uint useSharedMemForTonemapping;

  shader_uint usePointLightShadows;
  shader_uint useSpotLightShadows;
  shader_uint useDirectionalLightShadows;

  ShadowTechnique pointLightShadowsTechnique;
  ShadowTechnique spotLightShadowsTechnique;
  ShadowTechnique directionalLightShadowsTechnique;

  float histEqTonemappingRegW;
  float histEqTonemappingRefinedW;
  float histEqTonemappingMinAdmissibleLum;
  float histEqTonemappingMaxAdmissibleLum;

  // @TODO: try getting exposure from histogram instead
  float acesExposure;

  float csmSplitLambda;

  shader_uint pad1_, pad2_;
};

#endif // CONSTANTS_H_INCLUDED

