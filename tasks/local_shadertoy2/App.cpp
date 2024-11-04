#include "App.hpp"
#include "shaders/UniformParams.h"

#include <cstdint>
#include <cstring>
#include <etna/Etna.hpp>
#include <etna/Assert.hpp>
#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <glm/fwd.hpp>
#include <vulkan/vulkan_enums.hpp>
#include <vulkan/vulkan_structs.hpp>
#include <stb_image.h>


App::App()
  : resolution{1280, 720}
  , useVsync{true}
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
      .numFramesInFlight = 1,
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
    {LOCAL_SHADERTOY2_SHADERS_ROOT "toy.frag.spv", LOCAL_SHADERTOY2_SHADERS_ROOT "toy.vert.spv"});
  etna::create_program(
    "proc",
    {LOCAL_SHADERTOY2_SHADERS_ROOT "toy_buffer.frag.spv", LOCAL_SHADERTOY2_SHADERS_ROOT "toy.vert.spv"});

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

  {
    int texW, texH, texChannels;
    unsigned char* texData = stbi_load(
      GRAPHICS_COURSE_RESOURCES_ROOT "/textures/test_tex_1.png", &texW, &texH, &texChannels, 0);
    ETNA_VERIFY(texData);

    ETNA_VERIFYF(texChannels == 3 || texChannels == 4, "Invalid channels={}", texChannels);
    std::vector<std::byte> imageData{(size_t)(texH * texW * 4)};
    size_t dstId = 0;
    for (auto* src = texData; src != texData + texW * texH * texChannels; ++src)
    {
      imageData[dstId++] = (std::byte)(*src);
      if (texChannels == 3 && dstId % 4 == 3)
        imageData[dstId++] = (std::byte)255;
    }
    stbi_image_free(texData);

    sourceTexture = ctx.createImage(etna::Image::CreateInfo{
      .extent = vk::Extent3D{(uint32_t)texW, (uint32_t)texH, 1},
      .name = "src_tex",
      .format = vk::Format::eR8G8B8A8Srgb,
      .imageUsage = vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst});

    transferHelper->uploadImage(
      *oneShotCommands, sourceTexture, 0, 0, {imageData.cbegin(), imageData.size()});
  }

  defaultSampler = etna::Sampler{etna::Sampler::CreateInfo{.name = "default_sampler"}};
  detailSampler = etna::Sampler{etna::Sampler::CreateInfo{
    .filter = vk::Filter::eLinear,
    .addressMode = vk::SamplerAddressMode::eRepeat,
    .name = "default_sampler"}};

  uniformParams = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = sizeof(UniformParams),
    .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
    .name = "uniform_params",
  });

  uniformParams.map();

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

    params.iTime = static_cast<float>(windowing.getTime());
    params.iResolution = resolution;
    params.iMouse += osWindow->mouse.capturedPosDelta;
    memcpy(uniformParams.data(), &params, sizeof(params));

    drawFrame();
  }

  ETNA_CHECK_VK_RESULT(etna::get_context().getDevice().waitIdle());
}

void App::drawFrame()
{
  auto currentCmdBuf = commandManager->acquireNext();

  etna::begin_frame();

  auto nextSwapchainImage = vkWindow->acquireNext();

  if (nextSwapchainImage)
  {
    auto [backbuffer, backbufferView, backbufferAvailableSem] = *nextSwapchainImage;

    ETNA_CHECK_VK_RESULT(currentCmdBuf.begin(vk::CommandBufferBeginInfo{}));
    {
      {
        auto procProgInfo = etna::get_shader_program("proc");
        auto set = etna::create_descriptor_set(
          procProgInfo.getDescriptorLayoutId(0),
          currentCmdBuf,
          {etna::Binding{7, uniformParams.genBinding()}});

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

      {
        auto toyProgInfo = etna::get_shader_program("toy");
        auto set = etna::create_descriptor_set(
          toyProgInfo.getDescriptorLayoutId(0),
          currentCmdBuf,
          {etna::Binding{7, uniformParams.genBinding()}});
        auto imgSet = etna::create_descriptor_set(
          toyProgInfo.getDescriptorLayoutId(1),
          currentCmdBuf,
          {
            etna::Binding{
              0,
              proceduralImage.genBinding(
                defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
            etna::Binding{
              1,
              sourceTexture.genBinding(
                detailSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
          });

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
