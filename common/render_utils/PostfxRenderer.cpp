#include "PostfxRenderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/Etna.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/PipelineManager.hpp>


PostfxRenderer::PostfxRenderer(CreateInfo info)
{
  extent = info.extent;
  rtCount = 1 + info.secondaryAttachmentFormats.size();

  programId = etna::get_program_id(info.shaderProgramName.c_str());

  if (programId == etna::ShaderProgramId::Invalid)
  {
    programId = etna::create_program(
      info.shaderProgramName.c_str(),
      {RENDER_UTILS_SHADERS_ROOT "quad.vert.spv", info.fragShaderPath.c_str()});
  }

  std::vector<vk::Format> attachmentFormats{};
  std::vector<vk::PipelineColorBlendAttachmentState> attachments{};
  attachmentFormats.reserve(rtCount);
  attachmentFormats.push_back(info.format);
  for (auto fmt : info.secondaryAttachmentFormats)
    attachmentFormats.push_back(fmt);
  attachments.reserve(rtCount);
  for (int i = 0; i < rtCount; ++i)
  {
    attachments.push_back(vk::PipelineColorBlendAttachmentState{
      .blendEnable = vk::False,
      .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    });
  }

  auto& pipelineManager = etna::get_context().getPipelineManager();
  pipeline = pipelineManager.createGraphicsPipeline(
    info.shaderProgramName.c_str(),
    etna::GraphicsPipeline::CreateInfo{
      .blendingConfig = {.attachments = std::move(attachments)},
      .fragmentShaderOutput = {
        .colorAttachmentFormats = std::move(attachmentFormats),
      }});
}

void PostfxRenderer::render(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_ASSERT(rtCount == 1);
  etna::RenderTargetState renderTargets(
    cmd_buf,
    {{0, 0}, {extent.width, extent.height}},
    {{.image = target_image, .view = target_image_view, .loadOp = vk::AttachmentLoadOp::eClear}},
    {});

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipeline());

  cmd_buf.draw(3, 1, 0, 0);
}

void PostfxRenderer::render(
  vk::CommandBuffer cmd_buf,
  std::initializer_list<vk::Image> target_image,
  std::initializer_list<vk::ImageView> target_image_view)
{
  ETNA_ASSERT(target_image.size() == rtCount);
  ETNA_ASSERT(target_image_view.size() == rtCount);

  std::vector<etna::RenderTargetState::AttachmentParams> attachments;
  attachments.reserve(target_image.size());
  for (int i = 0; i < target_image.size(); ++i)
  {
    attachments.push_back(etna::RenderTargetState::AttachmentParams{
      .image = *(target_image.begin() + i),
      .view = *(target_image_view.begin() + i),
      .loadOp = vk::AttachmentLoadOp::eClear});
  }

  etna::RenderTargetState renderTargets(
    cmd_buf, {{0, 0}, {extent.width, extent.height}}, {std::move(attachments)}, {});

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipeline());

  cmd_buf.draw(3, 1, 0, 0);
}
