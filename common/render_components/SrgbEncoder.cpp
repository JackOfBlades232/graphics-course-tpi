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
    RENDER_COMPONENTS_SHADERS_ROOT
    "srgb_encode.slang-postfx_vertex_main-srgb_encode_fragment_main.escb",
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
    cmd_buf,
    encoder->shaderProgramInfo()
      .dsetBuilder()
      .setTex(
        "ldrImage", ldr_image.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal))
      .build());

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, encoder->pipelineLayout(), 0, {set.getVkSet()}, {});

  encoder->render(cmd_buf, target_image, target_image_view);
}
