#pragma once

#include <tiny_gltf.h>
#include <glm/glm.hpp>

#include <vector>
#include <optional>

struct JbTerrainExtDetailData
{
  std::string name;
  glm::vec2 uvScale{1.f, 1.f};
  glm::vec2 relHeightRange{0.f, 1.f};
  int splattingCompId{-1};
  int material{-1};
  int vegetation{-1};
  bool useSplattingMask{false};
  bool useRelHeightRange{false};
};

struct JbTerrainExtVegetationData
{
  std::string name;
  float radius;
  float sparsenessRadius;
  float height;
  int material{-1};
};

enum class JbTerrainExtContinentType
{
  CIRCLE = 1
};

struct JbTerrainExtContinentData
{
  JbTerrainExtContinentType type;
  float oceanBottom = 0.f;
  struct
  {
    glm::vec2 center{};
    float innerRad = 0.f;
    float outerRad = 0.f;
  } circle;
};

struct JbTerrainExtData
{
  int heightmap{-1};
  int splattingMask{-1};
  glm::vec3 rangeMin{-1.f, 0.f, -1.f}, rangeMax{1.f, 1.f, 1.f};

  int noiseSeed{0};

  std::optional<JbTerrainExtContinentData> continent;

  std::vector<JbTerrainExtDetailData> details{};
  std::vector<JbTerrainExtVegetationData> vegetations{};
};

struct JbTerrainExtMaterial
{
  int displacement{-1};
  float displacementCoeff{1.f};
};

std::optional<JbTerrainExtData> jb_terrain_parse_desc(const tinygltf::Model& model);
std::optional<JbTerrainExtMaterial> jb_terrain_parse_material_desc(const tinygltf::Material& mat);
