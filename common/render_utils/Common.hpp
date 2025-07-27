#pragma once

#include <etna/Vulkan.hpp>
#include <etna/Image.hpp>
#include <etna/Buffer.hpp>

#include <variant>


void emit_barriers(
  vk::CommandBuffer cmd_buf,
  std::initializer_list<const std::variant<vk::BufferMemoryBarrier2, vk::ImageMemoryBarrier2>>
    barriers);

void gen_mips(vk::CommandBuffer cmd_buf, etna::Image& img);

inline uint32_t mip_count_for_dims(uint32_t w, uint32_t h)
{
  return uint32_t(floorf(log2f(float(std::max(w, h)))) + 1.f);
}

etna::Buffer create_buffer(etna::Buffer::CreateInfo&& info);
etna::Image create_image(etna::Image::CreateInfo&& info);
