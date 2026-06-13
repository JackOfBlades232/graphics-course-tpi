#ifndef CONSTANTS_H_INCLUDED
#define CONSTANTS_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "geometry.h"
#include "lights.h"
#include "ssao.h"

#define BIG_EPSILON 0.001f

struct Constants
{
  shader_vec3 playerWorldPos;
  CullingMode cullingMode;

  shader_vec2 toroidalOffset;
  shader_vec2 toroidalUpdatePlayerWorldPos;

  float terrainNoiseRelHeightAmp;
  float terrainNoisePeriod;

  float vegetationRenderingDistance;
  float vegetationRenderingDropoffDistance;

  shader_uint terrainFirstInstance;

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

  shader_uint drawCascadesInSolidColor;

  float histEqTonemappingRegW;
  float histEqTonemappingRefinedW;
  float histEqTonemappingMinAdmissibleLum;
  float histEqTonemappingMaxAdmissibleLum;

  // @TODO: try getting exposure from histogram instead
  float acesExposure;

  float csmSplitLambda;
  float csmBlendingBeltSize;

  float windStrength;

  shader_vec2 windDirection;

  float time;
  float dt;

  shader_vec3 ambientLightCoeff;

  // HERE

  shader_uint useSsao;
  shader_uint useSkyboxForAmbient;

  shader_uint ssaoLimitSamples;
  float ssaoRadius;
  float ssaoBias;

  float ssaoPower;

  float ssaoEmaCoeff;
  float ssaoDepthRejectionThreshold;

  shader_uint ssaoConservariveTemporalCaching;

  shader_uint ssaoDoTemporalAccum;

  shader_uint ssaoTemporalAccumBacklog;
  shader_uint ssaoForceDropHistory;

  shader_uint frameNo;

  shader_vec2 mainTargetResolution;
  shader_vec2 mainTargetInverseResolution;

  shader_uint gammaEncodeInTonemapping;
  shader_uint fxaaAntialiasInSrgb;

  float taaEmaCoeff;

  float waterF;
  float waterH;
  float waterG;
  float waterRho;
  float waterSurfaceTension;
  float waterWindUnitsToMps;
  shader_uint waterEnabled;

  float pad1_, pad2_;

  SsaoConstData ssaoData;
};

#endif // CONSTANTS_H_INCLUDED
