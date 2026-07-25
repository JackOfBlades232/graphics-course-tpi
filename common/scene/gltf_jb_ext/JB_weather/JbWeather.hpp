#pragma once

#include <tiny_gltf.h>
#include <glm/glm.hpp>

#include <optional>


struct JbWeatherExtData
{
  struct Wind
  {
    glm::vec2 direction{1.f, 0.f};
    float strength = 0.f;
  } wind;
  struct Fog
  {
    float rho0 = 0.f;
    float hBase = 0.f;
    float hC = 0.f;
    struct Shape
    {
      float rngMin = 0.f;
      float rngMax = 1.f;
      float scale = 0.f;
      float windInfluence = 0.f;
      float fadeoutStart = 0.f;
      float fadeoutSize = 1.f;
    } shape;
    struct Inscatter
    {
      float c = 0.f;
    } inscatter;
  } fog;
};

std::optional<JbWeatherExtData> jb_weather_parse_desc(const tinygltf::Model& model);
