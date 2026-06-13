#pragma once

#include <tiny_gltf.h>

#include <optional>

struct JbWaterExtData
{
  float waterLevel = 0.f;
};

std::optional<JbWaterExtData> jb_water_parse_desc(const tinygltf::Model& model);
