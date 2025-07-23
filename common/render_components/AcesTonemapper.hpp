#pragma once

#include "ITonemapper.hpp"

#include <render_utils/PostfxRenderer.hpp>
#include <etna/GraphicsPipeline.hpp>


class AcesTonemapper final : public ITonemapper
{
public:
  AcesTonemapper() = default;

  void allocateResources(glm::uvec2 resolution) final;
  void loadShaders() final {}
  void setupPipelines(vk::Format swapchain_format, DebugDrawersRegistry&) final;

  void tonemap(
    vk::CommandBuffer cmd_buff,
    vk::Image target_image,
    vk::ImageView target_image_view,
    const etna::Image& hdr_image,
    const etna::Sampler& sampler,
    const etna::Buffer& constants) final;

private:
  std::unique_ptr<PostfxRenderer> tonemapper{};
  glm::uvec2 targetResolution;
};

