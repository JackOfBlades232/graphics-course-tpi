#pragma once

#include <tiny_gltf.h>
#include <glm/glm.hpp>

#include <vector>
#include <optional>

// @TODO: proper asset
// @TODO: some other way to get color -- diffuse is dumb

struct JbTerrainExtData
{
  int heightmap{-1}; 
  int diffuse{-1}; 
  int errosion{-1}; 

  glm::vec3 rangeMin{-1.f, 0.f, -1.f}, rangeMax{1.f, 1.f, 1.f};
  int errosionSeed{0};

  // @TODO: more  
};

std::optional<JbTerrainExtData> jb_terrain_parse_desc(const tinygltf::Model& model);
