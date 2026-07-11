#pragma once

#include <tiny_gltf.h>
#include <glm/glm.hpp>

#include <optional>

struct JbWaterExtSpectrum
{
  float shallowCutoff;
  float depth;
  float fetch;
  float scale;
  float gamma;
  float cascadeCutoffScale;
};

struct JbWaterExtShore
{
  float depth;
  float shallowPow;
  float steepMaxCoeff;
  float steepWeight;
  float permanentFoamWeight;
  float permanentFoamDepth;
  float permanentFoamF;
  float cyclicFoamWeight;
  float cyclicFoamDepth;
  float cyclicFoamF;
  float cyclicFoamSpeed;
};

struct JbWaterExtLightingCaustics
{
  float projectionFloorDepth;
  float tilelWorldSize;
  float tileApronUvSize;
  float lightScale;
  float renderDistance;
  float renderFadeout;
};

struct JbWaterExtLightingRefraction
{
  glm::vec3 color;
  float depth;
  float screen;
  std::optional<JbWaterExtLightingCaustics> caustics;
};

struct JbWaterExtLightingFoam
{
  glm::vec3 color;
  float roughness;
  float turbulenceBaseline;
  float turbulenceFadeout;
};

struct JbWaterExtLighting
{
  glm::vec3 surfaceColor;
  std::optional<JbWaterExtLightingRefraction> refraction;
  std::optional<JbWaterExtLightingFoam> foam;
};

struct JbWaterExtData
{
  float waterLevel = 0.f;
  std::vector<float> cascades{};
  JbWaterExtSpectrum spectrum{};
  JbWaterExtLighting lighting{};
  std::optional<JbWaterExtShore> shore{};
};

std::optional<JbWaterExtData> jb_water_parse_desc(const tinygltf::Model& model);
