#pragma once

#include <tiny_gltf.h>

#include <vector>
#include <optional>


struct JbTerrainExtData
{
  int heightmap{-1}; 
  int diffuse{-1}; 
  int errosion{-1}; 

  std::vector<double> heightmapRange{0.0, 1.0};
  int errosionSeed{0};

  // @TODO: more  
};

std::optional<JbTerrainExtData> jb_terrain_parse_desc(const tinygltf::Model& model);
