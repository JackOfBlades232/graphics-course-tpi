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
    {targetResolution.x, targetResolution.y},
    {vk::Format::eR32G32B32A32Sfloat}});
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

  auto set = etna::create_descriptor_set(
    aa->shaderProgramInfo().getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{
       0, aliased_image.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
     etna::Binding{1, resourceCb.prevFrameProvider()},
     etna::Binding{2, resourceCb.motionVectorsProvider()},
     etna::Binding{3, resourceCb.depthProvider()},
     etna::Binding{8, constants.genBinding()},
     etna::Binding{9, resourceCb.viewParamsProvider()}});

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, aa->pipelineLayout(), 0, {set.getVkSet()}, {});

  auto [curFrameTarget, curFrameTargetView] = resourceCb.curFrameProvider();
  aa->render(cmd_buf, {target_image, curFrameTarget}, {target_image_view, curFrameTargetView});
}
