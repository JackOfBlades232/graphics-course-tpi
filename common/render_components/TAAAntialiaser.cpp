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
    RENDER_COMPONENTS_SLANG_SHADERS_ROOT "taa.slang-postfx_vertex_main-taa_fragment_main.escb",
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
    cmd_buf,
    aa->shaderProgramInfo()
      .dsetBuilder()
      .setTex(
        "ldrImage",
        aliased_image.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal))
      .setTex("prevFrame", resourceCb.prevFrameProvider())
      .setTex("motionVectors", resourceCb.motionVectorsProvider())
      .setTex("gbufDepth", resourceCb.depthProvider())
      .setCbuf("constants", constants.genBinding())
      .setCbuf("viewParams", resourceCb.viewParamsProvider())
      .build());

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, aa->pipelineLayout(), 0, {set.getVkSet()}, {});

  auto [curFrameTarget, curFrameTargetView] = resourceCb.curFrameProvider();
  aa->render(cmd_buf, {target_image, curFrameTarget}, {target_image_view, curFrameTargetView});
}
