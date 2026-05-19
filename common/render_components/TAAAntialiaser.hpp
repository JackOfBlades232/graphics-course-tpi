#pragma once

#include "IAntialiaser.hpp"
#include "DebugDrawer.hpp"

#include <render_utils/PostfxRenderer.hpp>
#include <etna/GraphicsPipeline.hpp>


class TAAAntialiaser final : public IAntialiaser
{
public:
  TAAAntialiaser() = default;

  void allocateResources(glm::uvec2 resolution) final;
  void loadShaders() final {}
  void setupPipelines(vk::Format swapchain_format, DebugDrawersRegistry&) final;

  void antialias(
    vk::CommandBuffer cmd_buf,
    vk::Image target_image,
    vk::ImageView target_image_view,
    const etna::Image& aliased_image,
    const etna::Sampler& sampler,
    const etna::Buffer& constants) final;

private:
  std::unique_ptr<PostfxRenderer> aa{};
  glm::uvec2 targetResolution;
};


