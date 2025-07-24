#pragma once

#include <tiny_gltf.h>
#include <optional>


struct JbSkyboxExtData
{
  int cubemap{-1};
};

std::optional<JbSkyboxExtData> jb_skybox_parse_desc(const tinygltf::Model& model);
