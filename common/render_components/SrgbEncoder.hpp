#pragma once

#include "IComponent.hpp"
#include "DebugDrawer.hpp"

#include <render_utils/PostfxRenderer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>


class SrgbEncoder final : public IComponent
{
public:
  SrgbEncoder() = default;

  void allocateResources(glm::uvec2 resolution) final;
  void loadShaders() final {}
  void setupPipelines(vk::Format swapchain_format, DebugDrawersRegistry&) final;

  void encode(
    vk::CommandBuffer cmd_buff,
    vk::Image target_image,
    vk::ImageView target_image_view,
    const etna::Image& ldr_image,
    const etna::Sampler& sampler);

private:
  std::unique_ptr<PostfxRenderer> encoder{};
  glm::uvec2 targetResolution;
};

