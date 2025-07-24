#pragma once

#include <glm/glm.hpp>

#include <scene/SceneManager.hpp>


// @TODO: move stuff into here (resolution, vsync, etc)
struct Config
{
  bool testMultiplexScene = false;
  SceneMultiplexing testMultiplexing{};

  std::string debugConfigFile{"./debug_config.bin"};
  uint32_t debugConfigFileFormatVer = 6;
  bool useDebugConfig = true;
};
