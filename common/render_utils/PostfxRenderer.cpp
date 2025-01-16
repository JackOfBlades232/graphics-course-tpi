#include "PostfxRenderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/Etna.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/PipelineManager.hpp>


PostfxRenderer::PostfxRenderer(CreateInfo info)
{
  extent = info.extent;

  programId = etna::get_program_id(info.shaderProgramName.c_str());

  if (programId == etna::ShaderProgramId::Invalid)
  {
    programId = etna::create_program(
      info.shaderProgramName.c_str(),
      {RENDER_UTILS_SHADERS_ROOT "quad.vert.spv", info.fragShaderPath.c_str()});
  }

  auto& pipelineManager = etna::get_context().getPipelineManager();
  pipeline = pipelineManager.createGraphicsPipeline(
    info.shaderProgramName.c_str(),
    {
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {info.format},
        },
    });
}

void PostfxRenderer::render(
  vk::CommandBuffer cmd_buf,
  vk::Image target_image,
  vk::ImageView target_image_view)
{
  etna::RenderTargetState renderTargets(
    cmd_buf,
    {{0, 0}, {extent.width, extent.height}},
    {{.image = target_image, .view = target_image_view, .loadOp = vk::AttachmentLoadOp::eClear}},
    {});

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipeline());

  cmd_buf.draw(3, 1, 0, 0);
}
