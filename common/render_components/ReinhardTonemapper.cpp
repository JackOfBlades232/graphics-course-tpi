#include "ReinhardTonemapper.hpp"

#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>


void ReinhardTonemapper::allocateResources(glm::uvec2 resolution)
{
  targetResolution = resolution;
}

void ReinhardTonemapper::setupPipelines(vk::Format swapchain_format, DebugDrawersRegistry&)
{
  tonemapper = std::make_unique<PostfxRenderer>(PostfxRenderer::CreateInfo{
    "reinhard_tonemap",
    RENDER_COMPONENTS_SHADERS_ROOT "reinhard_tonemap.frag.spv",
    swapchain_format,
    {targetResolution.x, targetResolution.y}});
}

void ReinhardTonemapper::tonemap(
  vk::CommandBuffer cmd_buf,
  vk::Image target_image,
  vk::ImageView target_image_view,
  const etna::Image& hdr_image,
  const etna::Sampler& sampler,
  const etna::Buffer& constants)
{
  {
    ETNA_PROFILE_GPU(cmd_buf, reinhard_tonemapping_apply);

    auto set = etna::create_descriptor_set(
      tonemapper->shaderProgramInfo().getDescriptorLayoutId(0),
      cmd_buf,
      {etna::Binding{
         0, hdr_image.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
       etna::Binding{8, constants.genBinding()}});

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, tonemapper->pipelineLayout(), 0, {set.getVkSet()}, {});

    tonemapper->render(cmd_buf, target_image, target_image_view);
  }
}
