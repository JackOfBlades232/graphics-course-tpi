#include "QuadRenderer.hpp"

#include <etna/GlobalContext.hpp>
#include <etna/Etna.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/DescriptorSet.hpp>


const std::string_view QuadRenderer::VERTEX_SHADER_PATH = RENDER_UTILS_SHADERS_ROOT "quad.vert.spv";
const std::string_view QuadRenderer::FRAGMENT_SHADER_PATH =
  RENDER_UTILS_SHADERS_ROOT "quad.frag.spv";

QuadRenderer::QuadRenderer(CreateInfo info)
{
  programId = etna::get_program_id("quad_renderer");

  if (programId == etna::ShaderProgramId::Invalid)
    programId = etna::create_program("quad_renderer", {VERTEX_SHADER_PATH, FRAGMENT_SHADER_PATH});

  auto& pipelineManager = etna::get_context().getPipelineManager();
  pipeline = pipelineManager.createGraphicsPipeline(
    "quad_renderer",
    {
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {info.format},
        },
    });
}

void QuadRenderer::render(
  vk::CommandBuffer cmd_buf,
  vk::Image target_image,
  vk::ImageView target_image_view,
  vk::Rect2D rect,
  const etna::Image& tex_to_draw,
  const etna::Sampler& sampler,
  std::optional<uint32_t> layer,
  std::optional<uint32_t> mip_level,
  shader_vec2 color_range,
  bool showR,
  bool showG,
  bool showB,
  bool showA)
{
  auto programInfo = etna::get_shader_program(programId);
  auto set = etna::create_descriptor_set(
    programInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{
      0,
      tex_to_draw.genBinding(
        sampler.get(),
        vk::ImageLayout::eShaderReadOnlyOptimal,
        {.baseMip = mip_level ? *mip_level : 0,
         .levelCount = mip_level ? 1 : vk::RemainingMipLevels,
         .baseLayer = layer ? *layer : 0,
         .layerCount = layer ? 1 : vk::RemainingArrayLayers})}});

  etna::RenderTargetState renderTargets(
    cmd_buf,
    rect,
    {{.image = target_image, .view = target_image_view, .loadOp = vk::AttachmentLoadOp::eLoad}},
    {});

  cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipeline.getVkPipeline());
  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eGraphics, pipeline.getVkPipelineLayout(), 0, {set.getVkSet()}, {});

  struct PushParams
  {
    shader_vec2 colorRange;
    shader_uint colorMask;
    shader_uint pad1_;
  } params{};

  params.colorRange = color_range;
  params.colorMask = (showR ? 1 : 0) | (showG ? 2 : 0) | (showB ? 4 : 0) | (showA ? 8 : 0);

  cmd_buf.pushConstants<PushParams>(
    pipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eFragment, 0, params);

  cmd_buf.draw(3, 1, 0, 0);
}
