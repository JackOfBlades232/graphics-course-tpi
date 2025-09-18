#pragma once

#include <etna/Vulkan.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/RenderTargetStates.hpp>
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
  };

  explicit BboxRenderer(CreateInfo info);

  void render(
    vk::CommandBuffer cmd_buff,
    etna::RenderTargetState::RenderPassInfo&& rpi,
    const etna::Buffer& matrices,
    const etna::Buffer& instances,
    const etna::Buffer& bboxes,
    const etna::Buffer& constants,
    const etna::Buffer& view_params,
    uint32_t instance_count);

private:
  etna::GraphicsPipeline pipeline;
  etna::ShaderProgramId programId;

  BboxRenderer(const BboxRenderer&) = delete;
  BboxRenderer& operator=(const BboxRenderer&) = delete;
};
