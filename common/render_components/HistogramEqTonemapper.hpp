#pragma once

#include "ITonemapper.hpp"

#include <render_utils/PostfxRenderer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>


class HistogramEqTonemapper final : public ITonemapper
{
public:
  HistogramEqTonemapper() = default;

  void allocateResources(glm::uvec2 resolution) final;
  void loadShaders() final;
  void setupPipelines(vk::Format swapchain_format, DebugDrawersRegistry& debug_drawer_reg) final;

  void tonemap(
    vk::CommandBuffer cmd_buff,
    vk::Image target_image,
    vk::ImageView target_image_view,
    const etna::Image& hdr_image,
    const etna::Sampler& sampler,
    const etna::Buffer& constants) final;

private:
  etna::ComputePipeline clearPipeline{};
  etna::ComputePipeline minmaxPipeline{};
  etna::ComputePipeline preRefinePipeline{};
  etna::ComputePipeline binningPipeline{};
  etna::ComputePipeline distributionPipeline{};
  std::unique_ptr<PostfxRenderer> tonemapper{};

  etna::Buffer histData;
  etna::Buffer jndBinsData;
  glm::uvec2 targetResolution;
  uint32_t targetPixelCount;
  uint32_t jndBinsDataSize;

  etna::GraphicsPipeline debugPipeline{};

private:
  void computeHistogram(
    vk::CommandBuffer cmd_buf,
    const etna::Image& hdr_image,
    const etna::Sampler& sampler,
    const etna::Buffer& constants);
};
