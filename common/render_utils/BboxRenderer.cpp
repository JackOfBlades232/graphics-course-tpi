#include "BboxRenderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/Etna.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/DescriptorSet.hpp>


BboxRenderer::BboxRenderer(CreateInfo info)
{
  extent = info.extent;

  programId = etna::get_program_id("bbox_renderer");

  if (programId == etna::ShaderProgramId::Invalid)
    programId = etna::create_program(
      "bbox_renderer",
      {RENDER_UTILS_SHADERS_ROOT "bbox.vert.spv", RENDER_UTILS_SHADERS_ROOT "bbox.frag.spv"});

  auto& pipelineManager = etna::get_context().getPipelineManager();
  pipeline = pipelineManager.createGraphicsPipeline(
    "bbox_renderer",
    {
      .inputAssemblyConfig = {.topology = vk::PrimitiveTopology::eLineList},
      .rasterizationConfig =
        {
          .lineWidth = 1.f,
        },
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {info.format},
        },
    });
}

void BboxRenderer::render(
  vk::CommandBuffer cmd_buf,
  vk::Image target_image,
  vk::ImageView target_image_view,
  const etna::Buffer& matrices,
  const etna::Buffer& instances,
  const etna::Buffer& bboxes,
  const etna::Buffer& constants,
  uint32_t instance_count)
{
  auto programInfo = etna::get_shader_program(programId);
  auto set = etna::create_descriptor_set(
    programInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, matrices.genBinding()},
     etna::Binding{1, instances.genBinding()},
     etna::Binding{2, bboxes.genBinding()},
     etna::Binding{8, constants.genBinding()}});

  etna::RenderTargetState renderTargets(
    cmd_buf,
    {{0, 0}, {extent.width, extent.height}},
    {{.image = target_image, .view = target_image_view, .loadOp = vk::AttachmentLoadOp::eLoad}},
    {});

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipeline());
  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline.getVkPipelineLayout(), 0, {set.getVkSet()}, {});

  cmd_buf.draw(24, instance_count, 0, 0);
}
