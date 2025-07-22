#pragma once

#include "DebugDrawer.hpp"

#include <etna/Vulkan.hpp>
#include <glm/glm.hpp>


class IComponent
{
public:
  virtual ~IComponent() = default;

  virtual void allocateResources(glm::uvec2 resolution) = 0;
  virtual void loadShaders() = 0;
  virtual void setupPipelines(
    vk::Format swapchain_format, DebugDrawersRegistry& debug_drawer_reg) = 0;
};
