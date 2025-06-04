#pragma once

#include <etna/Vulkan.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/Buffer.hpp>


/**
 * Class for drawing bboxes for scene objects (without culling)
 */
class BboxRenderer
{
public:
  struct CreateInfo
  {
    vk::Format format = vk::Format::eUndefined;
    vk::Extent2D extent = {};
  };

  explicit BboxRenderer(CreateInfo info);

  void render(
    vk::CommandBuffer cmd_buff,
    vk::Image target_image,
    vk::ImageView target_image_view,
    const etna::Buffer& matrices,
    const etna::Buffer& instances,
    const etna::Buffer& bboxes,
    const etna::Buffer& constants,
    uint32_t instance_count);

private:
  etna::GraphicsPipeline pipeline;
  etna::ShaderProgramId programId;
  vk::Extent2D extent{};

  BboxRenderer(const BboxRenderer&) = delete;
  BboxRenderer& operator=(const BboxRenderer&) = delete;
};
