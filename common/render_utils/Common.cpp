#include "Common.hpp"

#include <etna/Vulkan.hpp>


void emit_barriers(
  vk::CommandBuffer cmd_buf,
  std::initializer_list<const std::variant<vk::BufferMemoryBarrier2, vk::ImageMemoryBarrier2>>
    barriers)
{
  std::vector<vk::BufferMemoryBarrier2> bufferBarriers{};
  std::vector<vk::ImageMemoryBarrier2> imageBarriers{};
  for (const auto& barrier : barriers)
  {
    std::visit(
      [&](const auto& b) {
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(b)>, vk::BufferMemoryBarrier2>)
          bufferBarriers.push_back(b);
        else
          imageBarriers.push_back(b);
      },
      barrier);
  }
  cmd_buf.pipelineBarrier2(vk::DependencyInfo{
    .dependencyFlags = vk::DependencyFlagBits::eByRegion,
    .bufferMemoryBarrierCount = uint32_t(bufferBarriers.size()),
    .pBufferMemoryBarriers = bufferBarriers.data(),
    .imageMemoryBarrierCount = uint32_t(imageBarriers.size()),
    .pImageMemoryBarriers = imageBarriers.data()});
}
