#include "App.hpp"
#include "etna/GpuWorkCount.hpp"
#include "etna/Profiling.hpp"
#include "shaders/UniformParams.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <thread>
#include <etna/Etna.hpp>
#include <etna/Assert.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <glm/fwd.hpp>
#include <tracy/Tracy.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_structs.hpp>
#include <stb_image.h>


using namespace std::chrono_literals;

App::App()
  : resolution{1280, 720}
  , useVsync{false}
  , gpuWorkCnt(3)
{
  {
    auto glfwInstExts = windowing.getRequiredVulkanInstanceExtensions();

    std::vector<const char*> instanceExtensions{glfwInstExts.begin(), glfwInstExts.end()};
    std::vector<const char*> deviceExtensions{VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    etna::initialize(etna::InitParams{
      .applicationName = "Local Shadertoy",
      .applicationVersion = VK_MAKE_VERSION(0, 1, 0),
      .instanceExtensions = instanceExtensions,
      .deviceExtensions = deviceExtensions,
      .physicalDeviceIndexOverride = {},
      .numFramesInFlight = (uint32_t)gpuWorkCnt.multiBufferingCount(),
    });
  }

  osWindow = windowing.createWindow(OsWindow::CreateInfo{
    .resolution = resolution,
  });

  osWindow->captureMouse = true;

  {
    auto surface = osWindow->createVkSurface(etna::get_context().getInstance());

    vkWindow = etna::get_context().createWindow(etna::Window::CreateInfo{
      .surface = std::move(surface),
    });

    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });

    resolution = {w, h};
  }

  commandManager = etna::get_context().createPerFrameCmdMgr();

  oneShotCommands = etna::get_context().createOneShotCmdMgr();
  transferHelper = std::make_unique<etna::BlockingTransferHelper>(
    etna::BlockingTransferHelper::CreateInfo{.stagingSize = 4096 * 4096 * 4});

  etna::create_program(
    "toy",
    {INFLIGHT_FRAMES_SHADERS_ROOT "toy.frag.spv", INFLIGHT_FRAMES_SHADERS_ROOT "toy.vert.spv"});
  etna::create_program(
    "proc",
    {INFLIGHT_FRAMES_SHADERS_ROOT "toy_buffer.frag.spv",
     INFLIGHT_FRAMES_SHADERS_ROOT "toy.vert.spv"});

  auto& ctx = etna::get_context();

  mainImage = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "main_image",
    .format = vkWindow->getCurrentFormat(),
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferSrc});
  proceduralImage = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{256, 256, 1},
    .name = "proc_image",
    .format = vkWindow->getCurrentFormat(),
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});

  auto getMipCountForDim = [](uint32_t w, uint32_t h) {
    return (uint32_t)floor(log2(std::max(w, h))) + 1;
  };
  auto generateTexMipLevels =
    [this](etna::Image& tex, uint32_t w, uint32_t h, uint32_t mip_count, uint32_t layer_count) {
      auto cmdBuf = oneShotCommands->start();

      ETNA_CHECK_VK_RESULT(cmdBuf.begin(vk::CommandBufferBeginInfo{}));
      {
        // Initial barrier
        etna::set_state(
          cmdBuf,
          tex.get(),
          vk::PipelineStageFlagBits2::eTransfer,
          vk::AccessFlagBits2::eTransferWrite,
          vk::ImageLayout::eTransferDstOptimal,
          vk::ImageAspectFlagBits::eColor);
        etna::flush_barriers(cmdBuf);

        for (uint32_t level = 1; level < mip_count; ++level)
        {
          {
            vk::ImageMemoryBarrier2 srcBarrier{
              .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
              .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
              .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
              .dstAccessMask = vk::AccessFlagBits2::eTransferRead,
              .oldLayout = vk::ImageLayout::eTransferDstOptimal,
              .newLayout = vk::ImageLayout::eTransferSrcOptimal,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = tex.get(),
              .subresourceRange = {vk::ImageAspectFlagBits::eColor, level - 1, 1, 0, layer_count}};
            vk::DependencyInfo depInfo{
              .dependencyFlags = vk::DependencyFlagBits::eByRegion,
              .imageMemoryBarrierCount = 1,
              .pImageMemoryBarriers = &srcBarrier};

            cmdBuf.pipelineBarrier2(depInfo);
          }

          vk::ImageBlit blit{
            .srcSubresource =
              {.aspectMask = vk::ImageAspectFlagBits::eColor,
               .mipLevel = level - 1,
               .baseArrayLayer = 0,
               .layerCount = layer_count},
            .dstSubresource = {
              .aspectMask = vk::ImageAspectFlagBits::eColor,
              .mipLevel = level,
              .baseArrayLayer = 0,
              .layerCount = layer_count}};
          blit.srcOffsets[0] = blit.dstOffsets[0] = {0, 0, 0};
          blit.srcOffsets[1] = {(int32_t)w, (int32_t)h, 1};
          blit.dstOffsets[1] = {(int32_t)std::max(w / 2, 1u), (int32_t)std::max(h / 2, 1u), 1};

          w = blit.dstOffsets[1].x;
          h = blit.dstOffsets[1].y;

          cmdBuf.blitImage(
            tex.get(),
            vk::ImageLayout::eTransferSrcOptimal,
            tex.get(),
            vk::ImageLayout::eTransferDstOptimal,
            {blit},
            vk::Filter::eLinear);

          // Now restore etna-tracked state
          {
            vk::ImageMemoryBarrier2 srcBarrier{
              .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
              .srcAccessMask = vk::AccessFlagBits2::eTransferRead,
              .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
              .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
              .oldLayout = vk::ImageLayout::eTransferSrcOptimal,
              .newLayout = vk::ImageLayout::eTransferDstOptimal,
              .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
              .image = tex.get(),
              .subresourceRange = {vk::ImageAspectFlagBits::eColor, level - 1, 1, 0, layer_count}};
            vk::DependencyInfo depInfo{
              .dependencyFlags = vk::DependencyFlagBits::eByRegion,
              .imageMemoryBarrierCount = 1,
              .pImageMemoryBarriers = &srcBarrier};

            cmdBuf.pipelineBarrier2(depInfo);
          }
        }

        etna::set_state(
          cmdBuf,
          tex.get(),
          vk::PipelineStageFlagBits2::eAllGraphics,
          vk::AccessFlagBits2::eShaderSampledRead,
          vk::ImageLayout::eShaderReadOnlyOptimal,
          vk::ImageAspectFlagBits::eColor);
        etna::flush_barriers(cmdBuf);
      }
      ETNA_CHECK_VK_RESULT(cmdBuf.end());

      oneShotCommands->submitAndWait(std::move(cmdBuf));
    };

  uint32_t detailMaxLod = 1;
  uint32_t skyboxMaxLod = 1;

  {
    int texW, texH, texChannels;
    unsigned char* texData = stbi_load(
      GRAPHICS_COURSE_RESOURCES_ROOT "/textures/test_tex_1.png", &texW, &texH, &texChannels, 4);
    ETNA_VERIFY(texData);

    uint32_t mipCnt = getMipCountForDim((uint32_t)texW, (uint32_t)texH);

    sourceTexture = ctx.createImage(etna::Image::CreateInfo{
      .extent = vk::Extent3D{(uint32_t)texW, (uint32_t)texH, 1},
      .name = "src_tex",
      .format = vk::Format::eR8G8B8A8Srgb,
      .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
        vk::ImageUsageFlagBits::eTransferDst,
      .mipLevels = mipCnt});

    transferHelper->uploadImage(
      *oneShotCommands,
      sourceTexture,
      0,
      0,
      std::span<const std::byte>{(const std::byte*)texData, (size_t)(texW * texH * 4)});
    stbi_image_free(texData);

    generateTexMipLevels(sourceTexture, texW, texH, mipCnt, 1);
    detailMaxLod = std::max(detailMaxLod, mipCnt);
  }

  {
    int texW, texH, texChannels;
    unsigned char* texData = stbi_load(
      GRAPHICS_COURSE_RESOURCES_ROOT "/textures/SomeSkyboxOffTheNet.png",
      &texW,
      &texH,
      &texChannels,
      4);
    ETNA_VERIFY(texData);

    uint32_t side = texW / 4;

    std::array<std::vector<std::byte>, 6> imageDatas{};
    std::array<glm::uvec2, 6> bases{
      glm::uvec2{2 * side, side},
      glm::uvec2{0, side},
      glm::uvec2{side, 0},
      glm::uvec2{side, 2 * side},
      glm::uvec2{side, side},
      glm::uvec2{3 * side, side}};

    for (size_t i = 0; i < 6; ++i)
    {
      auto& img = imageDatas[i];
      const auto& base = bases[i];

      img.resize(side * side * 4);
      size_t dstId = 0;
      for (uint32_t y = base.y; y < base.y + side; ++y)
        for (uint32_t x = base.x; x < base.x + side; ++x)
        {
          memcpy(img.data() + dstId, texData + (y * texW + x) * 4, 4);
          dstId += 4;
        }
    }
    stbi_image_free(texData);

    uint32_t mipCnt = getMipCountForDim(side, side);

    skyboxTexture = ctx.createImage(etna::Image::CreateInfo{
      .extent = vk::Extent3D{side, side, 1},
      .name = "skybox_tex",
      .format = vk::Format::eR8G8B8A8Srgb,
      .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferSrc |
        vk::ImageUsageFlagBits::eTransferDst,
      .layers = 6,
      .mipLevels = mipCnt,
      .flags = vk::ImageCreateFlagBits::eCubeCompatible});

    for (size_t i = 0; i < 6; ++i)
      transferHelper->uploadImage(*oneShotCommands, skyboxTexture, 0, (uint32_t)i, imageDatas[i]);
    generateTexMipLevels(skyboxTexture, side, side, mipCnt, 6);

    skyboxMaxLod = std::max(skyboxMaxLod, mipCnt);
  }

  defaultSampler = etna::Sampler{etna::Sampler::CreateInfo{.name = "default_sampler"}};
  detailSampler = etna::Sampler{etna::Sampler::CreateInfo{
    .filter = vk::Filter::eLinear,
    .addressMode = vk::SamplerAddressMode::eRepeat,
    .name = "detail_sampler",
    .maxLod = (float)detailMaxLod}};
  skyboxSampler = etna::Sampler{etna::Sampler::CreateInfo{
    .filter = vk::Filter::eLinear,
    .addressMode = vk::SamplerAddressMode::eClampToEdge,
    .name = "skybox_sampler",
    .maxLod = (float)skyboxMaxLod}};

  uniformParams.emplace(gpuWorkCnt, [&](size_t i) {
    return ctx.createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(UniformParams),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
      .name = "uniform_params" + std::string({(char)('0' + i)}),
    });
  });
  uniformParams->iterate([](etna::Buffer& buf) { buf.map(); });

  auto& pipelineManager = ctx.getPipelineManager();
  shadertoyPipeline = pipelineManager.createGraphicsPipeline(
    "toy",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {.colorAttachmentFormats = {vkWindow->getCurrentFormat()}}});
  proceduralPipeline = pipelineManager.createGraphicsPipeline(
    "proc",
    etna::GraphicsPipeline::CreateInfo{
      .fragmentShaderOutput = {.colorAttachmentFormats = {vkWindow->getCurrentFormat()}}});
}

App::~App()
{
  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::run()
{
  while (!osWindow->isBeingClosed())
  {
    windowing.poll();

    {
      ZoneScopedN("Input");

      params.iTime = static_cast<float>(windowing.getTime());
      params.iResolution = resolution;
      params.iMouse += osWindow->mouse.capturedPosDelta;
      memcpy(uniformParams->get().data(), &params, sizeof(params));

      // Fake work
      std::this_thread::sleep_for(7ms);
    }

    drawFrame();
    gpuWorkCnt.submit();

    FrameMark;
  }

  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::drawFrame()
{
  ZoneScoped;

  auto currentCmdBuf = commandManager->acquireNext();

  etna::begin_frame();

  auto nextSwapchainImage = vkWindow->acquireNext();

  if (nextSwapchainImage)
  {
    auto [backbuffer, backbufferView, backbufferAvailableSem] = *nextSwapchainImage;

    ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
    {
      ETNA_PROFILE_GPU(currentCmdBuf, renderFrame);

      {
        ETNA_PROFILE_GPU(currentCmdBuf, proceduralImage);

        auto procProgInfo = etna::get_shader_program("proc");
        auto set = etna::create_descriptor_set(
          procProgInfo.getDescriptorLayoutId(0),
          currentCmdBuf,
          {etna::Binding{7, uniformParams->get().genBinding()}});

        etna::RenderTargetState target{
          currentCmdBuf,
          {{0, 0}, {256, 256}},
          {{proceduralImage.get(), proceduralImage.getView({})}},
          {}};

        currentCmdBuf.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics,
          proceduralPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});
        currentCmdBuf.bindPipeline(
          vk::PipelineBindPoint::eGraphics, proceduralPipeline.getVkPipeline());

        currentCmdBuf.draw(3, 1, 0, 0);
      }

      // This should not be necessary, but smth is not set properly in descriptor creation
      etna::set_state(
        currentCmdBuf,
        proceduralImage.get(),
        vk::PipelineStageFlagBits2::eFragmentShader,
        vk::AccessFlagBits2::eShaderSampledRead,
        vk::ImageLayout::eShaderReadOnlyOptimal,
        vk::ImageAspectFlagBits::eColor);

      {
        ETNA_PROFILE_GPU(currentCmdBuf, mainImage);

        auto toyProgInfo = etna::get_shader_program("toy");
        auto set = etna::create_descriptor_set(
          toyProgInfo.getDescriptorLayoutId(0),
          currentCmdBuf,
          {etna::Binding{7, uniformParams->get().genBinding()}});
        auto imgSet = etna::create_descriptor_set(
          toyProgInfo.getDescriptorLayoutId(1),
          currentCmdBuf,
          {etna::Binding{
             0,
             proceduralImage.genBinding(
               defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
           etna::Binding{
             1,
             sourceTexture.genBinding(
               detailSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
           etna::Binding{
             2,
             skyboxTexture.genBinding(
               skyboxSampler.get(),
               vk::ImageLayout::eShaderReadOnlyOptimal,
               {.type = vk::ImageViewType::eCube})}});

        etna::RenderTargetState target{
          currentCmdBuf,
          {{0, 0}, {resolution.x, resolution.y}},
          {{mainImage.get(), mainImage.getView({})}},
          {}};

        currentCmdBuf.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics,
          shadertoyPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet(), imgSet.getVkSet()},
          {});
        currentCmdBuf.bindPipeline(
          vk::PipelineBindPoint::eGraphics, shadertoyPipeline.getVkPipeline());

        currentCmdBuf.draw(3, 1, 0, 0);
      }

      etna::set_state(
        currentCmdBuf,
        mainImage.get(),
        vk::PipelineStageFlagBits2::eTransfer,
        vk::AccessFlagBits2::eTransferRead,
        vk::ImageLayout::eTransferSrcOptimal,
        vk::ImageAspectFlagBits::eColor);
      etna::set_state(
        currentCmdBuf,
        backbuffer,
        vk::PipelineStageFlagBits2::eTransfer,
        vk::AccessFlagBits2::eTransferWrite,
        vk::ImageLayout::eTransferDstOptimal,
        vk::ImageAspectFlagBits::eColor);
      etna::flush_barriers(currentCmdBuf);

      {
        ETNA_PROFILE_GPU(currentCmdBuf, blit);

        vk::ImageBlit blit{
          .srcSubresource =
            {.aspectMask = vk::ImageAspectFlagBits::eColor,
             .mipLevel = 0,
             .baseArrayLayer = 0,
             .layerCount = 1},
          .dstSubresource = {
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1}};
        blit.srcOffsets[0] = blit.dstOffsets[0] = {0, 0, 0};
        blit.srcOffsets[1] = blit.dstOffsets[1] = {(int32_t)resolution.x, (int32_t)resolution.y, 1};

        currentCmdBuf.blitImage(
          mainImage.get(),
          vk::ImageLayout::eTransferSrcOptimal,
          backbuffer,
          vk::ImageLayout::eTransferDstOptimal,
          {blit},
          vk::Filter::eLinear);
      }

      etna::set_state(
        currentCmdBuf,
        backbuffer,
        vk::PipelineStageFlagBits2::eColorAttachmentOutput,
        {},
        vk::ImageLayout::ePresentSrcKHR,
        vk::ImageAspectFlagBits::eColor);
      etna::flush_barriers(currentCmdBuf);

      ETNA_READ_BACK_GPU_PROFILING(currentCmdBuf);
    }
    ETNA_CHECK_VK_RESULT(currentCmdBuf.end());

    auto renderingDone =
      commandManager->submit(std::move(currentCmdBuf), std::move(backbufferAvailableSem));

    const bool presented = vkWindow->present(std::move(renderingDone), backbufferView);

    if (!presented)
      nextSwapchainImage = std::nullopt;
  }

  etna::end_frame();

  if (!nextSwapchainImage && osWindow->getResolution() != glm::uvec2{0, 0})
  {
    auto [w, h] = vkWindow->recreateSwapchain(etna::Window::DesiredProperties{
      .resolution = {resolution.x, resolution.y},
      .vsync = useVsync,
    });
    ETNA_VERIFY((resolution == glm::uvec2{w, h}));
  }
}
