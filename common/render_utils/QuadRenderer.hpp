#pragma once

#include <etna/Vulkan.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include "shaders/cpp_glsl_compat.h"


/**
 * Simple class for displaying a texture on the screen for debug purposes.
 */
class QuadRenderer
{
public:
  struct CreateInfo
  {
    vk::Format format = vk::Format::eUndefined;
  };

  explicit QuadRenderer(CreateInfo info);

  // @TODO: tonemapping option for non-regular textures
  void render(
    vk::CommandBuffer cmd_buff,
    vk::Image target_image,
    vk::ImageView target_image_view,
    vk::Rect2D rect,
    const etna::Image& tex_to_draw,
    const etna::Sampler& sampler,
    std::optional<uint32_t> layer = std::nullopt,
    std::optional<uint32_t> mip_level = std::nullopt,
    shader_vec2 color_range = {0.f, 1.f},
    bool showR = true,
    bool showG = true,
    bool showB = true,
    bool showA = true);

private:
  etna::GraphicsPipeline pipeline;
  etna::ShaderProgramId programId;

  QuadRenderer(const QuadRenderer&) = delete;
  QuadRenderer& operator=(const QuadRenderer&) = delete;
};
