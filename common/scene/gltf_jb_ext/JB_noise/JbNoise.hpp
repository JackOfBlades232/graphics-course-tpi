#pragma once

#include <tiny_gltf.h>
#include <optional>


struct JbNoiseExtData
{
  struct Blue
  {
    int tex{-1};
  } blue;
};

std::optional<JbNoiseExtData> jb_noise_parse_desc(const tinygltf::Model& model);
