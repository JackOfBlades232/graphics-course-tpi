#include "FXAA311Antialiaser.hpp"

#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>


void FXAA311Antialiaser::allocateResources(glm::uvec2 resolution)
{
  targetResolution = resolution;
}

void FXAA311Antialiaser::setupPipelines(vk::Format swapchain_format, DebugDrawersRegistry&)
{
  aa = std::make_unique<PostfxRenderer>(PostfxRenderer::CreateInfo{
    "fxaa311",
    RENDER_COMPONENTS_SHADERS_ROOT "fxaa311.frag.spv",
    swapchain_format,
    {targetResolution.x, targetResolution.y}});
}

void FXAA311Antialiaser::antialias(
  vk::CommandBuffer cmd_buf,
  vk::Image target_image,
  vk::ImageView target_image_view,
  const etna::Image& aliased_image,
  const etna::Sampler& sampler,
  const etna::Buffer& constants)
{
  ETNA_PROFILE_GPU(cmd_buf, fxaa311);

  auto set = etna::create_descriptor_set(
    aa->shaderProgramInfo().getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{
       0, aliased_image.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
     etna::Binding{8, constants.genBinding()}});

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, aa->pipelineLayout(), 0, {set.getVkSet()}, {});

  aa->render(cmd_buf, target_image, target_image_view);
}

