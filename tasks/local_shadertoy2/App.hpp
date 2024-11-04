#pragma once

#include "shaders/UniformParams.h"

#include <etna/Window.hpp>
#include <etna/PerFrameCmdMgr.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/DescriptorSet.hpp>
#include <etna/Image.hpp>
#include <etna/Buffer.hpp>
#include <etna/Sampler.hpp>
#include <wsi/OsWindowingManager.hpp>


class App
{
public:
  App();
  ~App();

  void run();

private:
  void drawFrame();

private:
  OsWindowingManager windowing;
  std::unique_ptr<OsWindow> osWindow;

  glm::uvec2 resolution;
  bool useVsync;

  std::unique_ptr<etna::Window> vkWindow;
  std::unique_ptr<etna::PerFrameCmdMgr> commandManager;

  etna::GraphicsPipeline shadertoyPipeline;

  etna::Image mainImage, mainDepth;
  etna::Sampler defaultSampler;
  etna::Buffer uniformParams;

  UniformParams params{};
};
