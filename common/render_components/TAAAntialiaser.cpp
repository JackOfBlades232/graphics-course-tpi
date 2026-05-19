#include "TAAAntialiaser.hpp"

#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>


void TAAAntialiaser::allocateResources(glm::uvec2 resolution)
{
  targetResolution = resolution;
}

void TAAAntialiaser::setupPipelines(vk::Format swapchain_format, DebugDrawersRegistry&)
{
  aa = std::make_unique<PostfxRenderer>(PostfxRenderer::CreateInfo{
    "taa",
    RENDER_COMPONENTS_SHADERS_ROOT "taa.frag.spv",
    swapchain_format,
    {targetResolution.x, targetResolution.y}});
}

void TAAAntialiaser::antialias(
  vk::CommandBuffer cmd_buf,
  vk::Image target_image,
  vk::ImageView target_image_view,
  const etna::Image& aliased_image,
  const etna::Sampler& sampler,
  const etna::Buffer& constants)
{
  ETNA_PROFILE_GPU(cmd_buf, taa);

  // @TODO: impl

  (void)target_image;
  (void)target_image_view;
  (void)aliased_image;
  (void)sampler;
  (void)constants;
  ETNA_ASSERT(0);
}

