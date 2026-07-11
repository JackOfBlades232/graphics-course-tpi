#pragma once

#include <tiny_gltf.h>
#include <glm/glm.hpp>

#include <optional>


struct JbWindExtData
{
  glm::vec2 direction{1.f, 0.f};
  float strength = 0.f;
};

std::optional<JbWindExtData> jb_wind_parse_desc(const tinygltf::Model& model);
