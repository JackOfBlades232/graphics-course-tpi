#pragma once

#ifdef __cplusplus
#include <glm/glm.hpp>
#define int2 glm::ivec2
#define int3 glm::ivec3
#define uint glm::uint
#define uint2 glm::uvec2
#define uint3 glm::uvec3
#define float float
#define float2 glm::vec2
#define float3 glm::vec3
#define float4 glm::vec4
#define float4x3 glm::mat4x3
#define float4x4 glm::mat4x4
#define bool glm::uint
#endif

// @TODO: reuse, reorg fields, drop everything here, optional namespaces

// Scene
//

enum class CullingMode : uint
{
  PER_VERTEX = 0,
  SAT = 1
};

// Materials
//

enum class MaterialType : uint
{
  PBR = 1,
  DIFFUSE = 2
};

enum class TexId : uint16_t
{
  INVALID = uint16_t(-1)
};

enum class SmpId : uint16_t
{
  INVALID = uint16_t(-1)
};

enum class TexSmpIdPair : uint
{
  INVALID = uint(-1)
};

struct TexSmpIdPairUnpacked
{
  TexId tid;
  SmpId sid;
};

enum class MaterialId : uint
{
  INVALID = uint(-1)
};

// Lights
//

enum class ShadowTechnique : uint
{
  SIMPLE = 0,
  PCF3X3,
  PCF5X5,
  PCF7X7,
  PCF9X9,

  // @TODO: VSM

  COUNT
};

// SSAO
//

static const int SSAO_KERNEL_MAX_SIZE = 128;
static const int SSAO_BLUR_KERNEL_SIZE =  5;
static const int SSAO_BLUR_KERNEL_HS = (SSAO_BLUR_KERNEL_SIZE / 2);
static const int SSAO_BLUR_KERNEL_TSIZE = (SSAO_BLUR_KERNEL_SIZE * SSAO_BLUR_KERNEL_SIZE);
static const int SSAO_BLUR_KERNEL_TSIZE_V4 = ((SSAO_BLUR_KERNEL_TSIZE + (SSAO_BLUR_KERNEL_TSIZE & 1)) / 2);

struct SsaoConstData
{
  float4 ssaoKernel[SSAO_KERNEL_MAX_SIZE];
  float4 ssaoKernelRotations[SSAO_BLUR_KERNEL_TSIZE_V4];
};

// Weather
//

struct WeatherSourceData
{
  float2 windDirection;
  float windStrength;
  float fogRho0;
  float fogHBase;
  float fogHC;
  float fogShapeRngMin;
  float fogShapeRngMax;
  float fogShapeScale;
  float fogShapeWindInfluence;
  float fogShapeFadeoutStart;
  float fogShapeFadeoutSize;
  float fogInscatterC;
  float pad1_, pad2_, pad3_;
};

// Water
//

static const int WATER_CASCADE_COUNT = 3;
static const int WATER_CASCADE_RES_POW = 8;
static const int WATER_CASCADE_RES = (1 << WATER_CASCADE_RES_POW);

static const int WATER_WORKGROUP_DIM = 16;

// 2 buffers per cascade -- (x, y, z, dXZ), (dYX, dYZ, dXX, dZZ)
static const int WATER_FFT_BUFFER_COUNT = (2 * WATER_CASCADE_COUNT);

static const int WATER_TESSHQUAD_LEVEL_COUNT = 8;
static const float WATER_TESSHQUAD_EXTENT_STEP = 10.f;

static const int WATER_TESSHQUAD_CHUNKS_LEVEL_DIM = 4;
static const int WATER_TESSHQUAD_FIRST_LEVEL_CHUNKS =
  (WATER_TESSHQUAD_CHUNKS_LEVEL_DIM * WATER_TESSHQUAD_CHUNKS_LEVEL_DIM);
static const int WATER_TESSHQUAD_OTHER_LEVELS_CHUNKS =
  (WATER_TESSHQUAD_FIRST_LEVEL_CHUNKS - (WATER_TESSHQUAD_FIRST_LEVEL_CHUNKS / 4));
static const int WATER_TESSHQUAD_TOTAL_CHUNK_COUNT =
  (WATER_TESSHQUAD_FIRST_LEVEL_CHUNKS +
   (WATER_CASCADE_COUNT - 1) * WATER_TESSHQUAD_OTHER_LEVELS_CHUNKS);

static const int WATER_TESSHQUAD_CHUNK_TESSELLATION_FACTOR = 64;

static const int WATER_CAUSTIC_MAP_RES = 1024;

struct WaterSourceData
{
  float waterLevel;
  float l0;
  float l1;
  float l2;

  float spectrumShallowCutoff;
  float spectrumDepth;
  float spectrumFetch;
  float spectrumScale;
  float spectrumGamma;
  float spectrumCascadeCutoffScale;

  float lightingRefractionDepth;
  float lightingRefractionScreen;

  float lightingCausticsProjectionFloorDepth;
  float lightingCausticsTilelWorldSize;
  float lightingCausticsTileApronUvSize;
  float lightingCausticsLightScale;
  float lightingCausticsRenderDistance;
  float lightingCausticsRenderFadeout;

  float shoreDepth;
  float shoreShallowPow;
  float shoreSteepMaxCoeff;
  float shoreSteepWeight;
  float shorePermanentFoamWeight;
  float shorePermanentFoamDepth;
  float shorePermanentFoamF;
  float shoreCyclicFoamWeight;
  float shoreCyclicFoamDepth;
  float shoreCyclicFoamF;
  float shoreCyclicFoamSpeed;

  float foamRoughness;
  float foamTurbulenceBaseline;
  float foamTurbulenceFadeout;

  float3 lightingSurfaceColor;

  uint hasShore;

  float3 lightingRefractionColor;

  uint hasRefraction;

  float3 lightingFoamColor;

  uint hasFoam;

  uint hasCaustics;

  uint pad0_, pad1_, pad2_;
};

// All global params
//

struct Constants
{
  float3 playerWorldPos;
  CullingMode cullingMode;

  float2 toroidalOffset;
  float2 toroidalUpdatePlayerWorldPos;

  float terrainNoiseRelHeightAmp;
  float terrainNoisePeriod;

  float vegetationRenderingDistance;
  float vegetationRenderingDropoffDistance;

  uint terrainFirstInstance;

  uint drawTerrainSplattedDetail;

  bool useSkybox;

  bool useTonemapping;
  bool useSharedMemForTonemapping;

  bool usePointLightShadows;
  bool useSpotLightShadows;
  bool useDirectionalLightShadows;

  ShadowTechnique pointLightShadowsTechnique;
  ShadowTechnique spotLightShadowsTechnique;
  ShadowTechnique directionalLightShadowsTechnique;

  bool drawCascadesInSolidColor;

  float histEqTonemappingRegW;
  float histEqTonemappingRefinedW;
  float histEqTonemappingMinAdmissibleLum;
  float histEqTonemappingMaxAdmissibleLum;

  // @TODO: try getting exposure from histogram instead
  float acesExposure;

  float csmSplitLambda;
  float csmBlendingBeltSize;

  float padx_;

  float time;
  float dt;

  float pady_, padz_;

  float3 ambientLightCoeff;

  // HERE

  bool useSsao;
  bool useSkyboxForAmbient;

  uint ssaoLimitSamples;
  float ssaoRadius;
  float ssaoBias;

  float ssaoPower;

  float ssaoEmaCoeff;
  float ssaoDepthRejectionThreshold;

  bool ssaoConservativeTemporalCaching;

  bool ssaoDoTemporalAccum;

  uint ssaoTemporalAccumBacklog;
  bool ssaoForceDropHistory;

  uint frameNo;

  float2 mainTargetResolution;
  float2 mainTargetInverseResolution;

  bool gammaEncodeInTonemapping;
  bool fxaaAntialiasInSrgb;

  float taaEmaCoeff;

  bool waterEnabled;

  TexSmpIdPair blueNoiseTexSmp;

  int fogRaymarchSteps;
  int fogDistantSteps;
  float fogMaxStepSize;
  float fogSceneLightsScatterCoeff;

  float pad0_, pad1_, pad2_;

  WeatherSourceData weatherData;

  SsaoConstData ssaoData;
};

#ifdef __cplusplus
#undef int2
#undef int3
#undef uint
#undef uint2
#undef uint3
#undef float
#undef float2
#undef float3
#undef float4
#undef float4x3
#undef float4x4
#undef bool
#endif
