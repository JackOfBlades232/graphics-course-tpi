#pragma once

#include <etna/Vulkan.hpp>

#include <variant>


void emit_barriers(
  vk::CommandBuffer cmd_buf,
  std::initializer_list<const std::variant<vk::BufferMemoryBarrier2, vk::ImageMemoryBarrier2>>
    barriers);
