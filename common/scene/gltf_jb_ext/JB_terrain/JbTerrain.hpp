#pragma once

#include <tiny_gltf.h>
#include <glm/glm.hpp>

#include <vector>
#include <optional>

// @TODO: proper asset
// @TODO: some other way to get color -- diffuse is dumb

// @TODO: implement splatting mask

struct JbTerrainExtDetailData
{
  std::string name;
  glm::vec2 uvScale{1.f, 1.f};
  glm::vec2 relHeightRange{0.f, 1.f};
  int splattingCompId{-1};
  glm::uint splattingCompMask{0};
  int material{-1};
  bool useSplattingMask{false};
  bool useRelHeightRange{false};
};

struct JbTerrainExtData
{
  int heightmap{-1}; 
  int splattingMask{-1}; 
  glm::vec3 rangeMin{-1.f, 0.f, -1.f}, rangeMax{1.f, 1.f, 1.f};

  int noiseSeed{0};

  std::vector<JbTerrainExtDetailData> details{};

  // @TODO: more
};

struct JbTerrainExtMaterial
{
  int displacement{-1};
  float displacementCoeff{1.f};
};

std::optional<JbTerrainExtData> jb_terrain_parse_desc(const tinygltf::Model& model);
std::optional<JbTerrainExtMaterial> jb_terrain_parse_material_desc(const tinygltf::Material& mat);
