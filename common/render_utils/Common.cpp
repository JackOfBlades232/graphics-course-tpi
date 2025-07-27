#include "Common.hpp"

#include <etna/Vulkan.hpp>
#include <etna/Etna.hpp>
#include <etna/GlobalContext.hpp>


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

void gen_mips(vk::CommandBuffer cmd_buf, etna::Image& img)
{
  // @TODO: handle 3d textures?
  auto [w, h, _] = img.getExtent();
  uint32_t mipCount = uint32_t(img.getMipLevelCount());
  uint32_t layerCount = uint32_t(img.getLayerCount());

  // Initial barrier
  etna::set_state(
    cmd_buf,
    img.get(),
    vk::PipelineStageFlagBits2::eTransfer,
    vk::AccessFlagBits2::eTransferWrite,
    vk::ImageLayout::eTransferDstOptimal,
    vk::ImageAspectFlagBits::eColor);
  etna::flush_barriers(cmd_buf);

  for (uint32_t level = 1; level < mipCount; ++level)
  {
    emit_barriers(
      cmd_buf,
      {vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
        .oldLayout = vk::ImageLayout::eTransferDstOptimal,
        .newLayout = vk::ImageLayout::eTransferSrcOptimal,
        .image = img.get(),
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, level - 1, 1, 0, layerCount}}});

    vk::ImageBlit blit{
      .srcSubresource =
        {.aspectMask = vk::ImageAspectFlagBits::eColor,
         .mipLevel = level - 1,
         .baseArrayLayer = 0,
         .layerCount = layerCount},
      .dstSubresource = {
        .aspectMask = vk::ImageAspectFlagBits::eColor,
        .mipLevel = level,
        .baseArrayLayer = 0,
        .layerCount = layerCount}};
    blit.srcOffsets[0] = blit.dstOffsets[0] = {0, 0, 0};
    blit.srcOffsets[1] = {int32_t(w), int32_t(h), 1};
    blit.dstOffsets[1] = {int32_t(std::max(w / 2u, 1u)), int32_t(std::max(h / 2u, 1u)), 1};

    w = blit.dstOffsets[1].x;
    h = blit.dstOffsets[1].y;

    cmd_buf.blitImage(
      img.get(),
      vk::ImageLayout::eTransferSrcOptimal,
      img.get(),
      vk::ImageLayout::eTransferDstOptimal,
      {blit},
      vk::Filter::eLinear);

    // Now restore etna-tracked state
    emit_barriers(
      cmd_buf,
      {vk::ImageMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
        .newLayout = vk::ImageLayout::eTransferDstOptimal,
        .image = img.get(),
        .subresourceRange = {vk::ImageAspectFlagBits::eColor, level - 1, 1, 0, layerCount}}});
  }
}

etna::Buffer create_buffer(etna::Buffer::CreateInfo&& info)
{
  auto minfo = std::move(info);
  minfo.size = std::max(minfo.size, vk::DeviceSize{1}); // Vma won't do empty allocs
  return etna::get_context().createBuffer(std::move(minfo));
}

etna::Image create_image(etna::Image::CreateInfo&& info)
{
  auto minfo = std::move(info);
  minfo.extent = {
    std::max(minfo.extent.width, uint32_t{1}),
    std::max(minfo.extent.height, uint32_t{1}),
    std::max(minfo.extent.depth, uint32_t{1})};
  return etna::get_context().createImage(std::move(minfo));
}
