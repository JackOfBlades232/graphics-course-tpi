#include "SrgbEncoder.hpp"

#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>


void SrgbEncoder::allocateResources(glm::uvec2 resolution)
{
  targetResolution = resolution;
}

void SrgbEncoder::setupPipelines(vk::Format swapchain_format, DebugDrawersRegistry&)
{
  encoder = std::make_unique<PostfxRenderer>(PostfxRenderer::CreateInfo{
    "srgb_encode",
    RENDER_COMPONENTS_SHADERS_ROOT "srgb_encode.frag.spv",
    swapchain_format,
    {targetResolution.x, targetResolution.y}});
}

void SrgbEncoder::encode(
  vk::CommandBuffer cmd_buf,
  vk::Image target_image,
  vk::ImageView target_image_view,
  const etna::Image& ldr_image,
  const etna::Sampler& sampler)
{
  ETNA_PROFILE_GPU(cmd_buf, srgb_encode);

  auto set = etna::create_descriptor_set(
    encoder->shaderProgramInfo().getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{
      0, ldr_image.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}});

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, encoder->pipelineLayout(), 0, {set.getVkSet()}, {});

  encoder->render(cmd_buf, target_image, target_image_view);
}
