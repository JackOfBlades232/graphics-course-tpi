#pragma once

#include <etna/Vulkan.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/Etna.hpp>

#include <initializer_list>

/**
 * Class for rendering a full screen effect with a fragment shader
 */
class PostfxRenderer
{
public:
  struct CreateInfo
  {
    std::string shaderProgramName{};
    std::string fragShaderPath{};
    vk::Format format = vk::Format::eUndefined;
    vk::Extent2D extent = {};
    std::vector<vk::Format> secondaryAttachmentFormats{};
  };

  explicit PostfxRenderer(CreateInfo info);

  void render(vk::CommandBuffer cmd_buff, vk::Image target_image, vk::ImageView target_image_view);
  void render(
    vk::CommandBuffer cmd_buff,
    std::initializer_list<vk::Image> target_image,
    std::initializer_list<vk::ImageView> target_image_view);

  etna::ShaderProgramInfo shaderProgramInfo() const { return etna::get_shader_program(programId); }
  vk::PipelineLayout pipelineLayout() const { return pipeline.getVkPipelineLayout(); }

private:
  etna::GraphicsPipeline pipeline;
  etna::ShaderProgramId programId;
  vk::Extent2D extent{};
  size_t rtCount = 1;

  PostfxRenderer(const PostfxRenderer&) = delete;
  PostfxRenderer& operator=(const PostfxRenderer&) = delete;
};
