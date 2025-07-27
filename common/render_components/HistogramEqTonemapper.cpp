#include "HistogramEqTonemapper.hpp"

#include "shaders/histogram_tonemapping.h"
#include <render_utils/QuadRenderer.hpp>
#include <render_utils/Common.hpp>
#include <utils/Common.hpp>

#include <draw.h>

#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>


void HistogramEqTonemapper::allocateResources(glm::uvec2 resolution)
{
  targetResolution = resolution;
  targetPixelCount = targetResolution.x * targetResolution.y;
  jndBinsDataSize = align_up_pot(div_enough(targetPixelCount, 8u), 4u);

  histData = create_buffer(etna::Buffer::CreateInfo{
    .size = sizeof(HistogramData),
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "hist_data"});
  jndBinsData = create_buffer(etna::Buffer::CreateInfo{
    .size = jndBinsDataSize,
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "jnd_bins_buffer"});
}

void HistogramEqTonemapper::loadShaders()
{
  etna::create_program(
    "histogram_clear", {RENDER_COMPONENTS_SHADERS_ROOT "histogram_clear.comp.spv"});
  etna::create_program(
    "histogram_minmax", {RENDER_COMPONENTS_SHADERS_ROOT "histogram_minmax.comp.spv"});
  etna::create_program(
    "histogram_binning", {RENDER_COMPONENTS_SHADERS_ROOT "histogram_binning.comp.spv"});
  etna::create_program(
    "histogram_pre_refine", {RENDER_COMPONENTS_SHADERS_ROOT "histogram_pre_refine.comp.spv"});
  etna::create_program(
    "histogram_distribution", {RENDER_COMPONENTS_SHADERS_ROOT "histogram_distribution.comp.spv"});
  etna::create_program(
    "histogram_debug",
    {RENDER_COMPONENTS_SHADERS_ROOT "histogram_debug.frag.spv", QuadRenderer::VERTEX_SHADER_PATH});
}

void HistogramEqTonemapper::setupPipelines(
  vk::Format swapchain_format, DebugDrawersRegistry& debug_drawer_reg)
{
  auto& pipelineManager = etna::get_context().getPipelineManager();

  clearPipeline = pipelineManager.createComputePipeline("histogram_clear", {});
  minmaxPipeline = pipelineManager.createComputePipeline("histogram_minmax", {});
  binningPipeline = pipelineManager.createComputePipeline("histogram_binning", {});
  preRefinePipeline = pipelineManager.createComputePipeline("histogram_pre_refine", {});
  distributionPipeline = pipelineManager.createComputePipeline("histogram_distribution", {});

  debugPipeline = pipelineManager.createGraphicsPipeline(
    "histogram_debug",
    {
      .blendingConfig =
        {.attachments = {vk::PipelineColorBlendAttachmentState{
           .blendEnable = vk::True,
           .srcColorBlendFactor = vk::BlendFactor::eSrcAlpha,
           .dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha,
           .colorBlendOp = vk::BlendOp::eAdd,
           .srcAlphaBlendFactor = vk::BlendFactor::eOne,
           .dstAlphaBlendFactor = vk::BlendFactor::eOne,
           .alphaBlendOp = vk::BlendOp::eAdd,
           .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
             vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA}},
         .logicOp = vk::LogicOp::eSet},
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = {swapchain_format},
        },
    });

  tonemapper = std::make_unique<PostfxRenderer>(PostfxRenderer::CreateInfo{
    "histogram_tonemap",
    RENDER_COMPONENTS_SHADERS_ROOT "histogram_tonemap.frag.spv",
    swapchain_format,
    {targetResolution.x, targetResolution.y}});

  debug_drawer_reg.emplace(
    "histrogram_debug_view",
    DebugDrawer{
      [this](vk::CommandBuffer cb, vk::Image ti, vk::ImageView tiv) {
        auto programInfo = etna::get_shader_program("histogram_debug");
        auto set = etna::create_descriptor_set(
          programInfo.getDescriptorLayoutId(0), cb, {etna::Binding{0, histData.genBinding()}});

        etna::RenderTargetState renderTargets{
          cb,
          {{0, 0}, {targetResolution.x / 2, targetResolution.y / 2}},
          {{.image = ti, .view = tiv, .loadOp = vk::AttachmentLoadOp::eLoad}},
          {}};

        cb.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics,
          debugPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, debugPipeline.getVkPipeline());

        cb.draw(3, 1, 0, 0);
      },
      [] {}});
}

void HistogramEqTonemapper::tonemap(
  vk::CommandBuffer cmd_buf,
  vk::Image target_image,
  vk::ImageView target_image_view,
  const etna::Image& hdr_image,
  const etna::Sampler& sampler,
  const etna::Buffer& constants)
{
  {
    ETNA_PROFILE_GPU(cmd_buf, histogram_tonemapping_compute);
    computeHistogram(cmd_buf, hdr_image, sampler, constants);
  }

  {
    ETNA_PROFILE_GPU(cmd_buf, histogram_tonemapping_apply);

    auto set = etna::create_descriptor_set(
      tonemapper->shaderProgramInfo().getDescriptorLayoutId(0),
      cmd_buf,
      {etna::Binding{
         0, hdr_image.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
       etna::Binding{1, histData.genBinding()},
       etna::Binding{8, constants.genBinding()}});

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, tonemapper->pipelineLayout(), 0, {set.getVkSet()}, {});

    tonemapper->render(cmd_buf, target_image, target_image_view);
  }
}

void HistogramEqTonemapper::computeHistogram(
  vk::CommandBuffer cmd_buf,
  const etna::Image& hdr_image,
  const etna::Sampler& sampler,
  const etna::Buffer& constants)
{
  emit_barriers(
    cmd_buf,
    {vk::BufferMemoryBarrier2{
       .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
       .srcAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
       .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
       .buffer = histData.get(),
       .size = sizeof(HistogramData)},
     vk::BufferMemoryBarrier2{
       .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .srcAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
       .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
       .buffer = jndBinsData.get(),
       .size = jndBinsDataSize}});

  {
    ETNA_PROFILE_GPU(cmd_buf, histogram_clear);

    auto programInfo = etna::get_shader_program("histogram_clear");
    auto set = etna::create_descriptor_set(
      programInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {etna::Binding{0, histData.genBinding()}, etna::Binding{1, jndBinsData.genBinding()}});

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute,
      clearPipeline.getVkPipelineLayout(),
      0,
      {set.getVkSet()},
      {});
    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, clearPipeline.getVkPipeline());

    cmd_buf.dispatch(
      get_linear_wg_count(
        std::max<uint32_t>(jndBinsDataSize / sizeof(shader_uint), HISTOGRAM_BINS),
        HISTOGRAM_WORK_GROUP_SIZE),
      1,
      1);
  }

  emit_barriers(
    cmd_buf,
    {vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask =
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
      .buffer = histData.get(),
      .size = sizeof(HistogramData)}});

  etna::set_state(
    cmd_buf,
    hdr_image.get(),
    vk::PipelineStageFlagBits2::eFragmentShader | vk::PipelineStageFlagBits2::eComputeShader,
    vk::AccessFlagBits2::eShaderSampledRead,
    vk::ImageLayout::eShaderReadOnlyOptimal,
    vk::ImageAspectFlagBits::eColor);
  etna::flush_barriers(cmd_buf);

  {
    ETNA_PROFILE_GPU(cmd_buf, histogram_minmax);

    auto programInfo = etna::get_shader_program("histogram_minmax");
    auto set = etna::create_descriptor_set(
      programInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {etna::Binding{
         0, hdr_image.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
       etna::Binding{1, histData.genBinding()},
       etna::Binding{8, constants.genBinding()}});

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute,
      minmaxPipeline.getVkPipelineLayout(),
      0,
      {set.getVkSet()},
      {});
    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, minmaxPipeline.getVkPipeline());

    cmd_buf.dispatch(
      get_linear_wg_count(
        targetPixelCount, HISTOGRAM_WORK_GROUP_SIZE * HISTOGRAM_PIXELS_PER_THREAD),
      1,
      1);
  }

  emit_barriers(
    cmd_buf,
    {vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask =
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask =
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
      .buffer = histData.get(),
      .size = sizeof(HistogramData)}});

  {
    ETNA_PROFILE_GPU(cmd_buf, histogram_pre_refine);

    auto programInfo = etna::get_shader_program("histogram_pre_refine");
    auto set = etna::create_descriptor_set(
      programInfo.getDescriptorLayoutId(0), cmd_buf, {etna::Binding{0, histData.genBinding()}});

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute,
      preRefinePipeline.getVkPipelineLayout(),
      0,
      {set.getVkSet()},
      {});
    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, preRefinePipeline.getVkPipeline());

    cmd_buf.dispatch(1, 1, 1);
  }

  emit_barriers(
    cmd_buf,
    {vk::BufferMemoryBarrier2{
       .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .srcAccessMask =
         vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
       .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .dstAccessMask =
         vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
       .buffer = histData.get(),
       .size = sizeof(HistogramData)},
     vk::BufferMemoryBarrier2{
       .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
       .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .dstAccessMask =
         vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
       .buffer = jndBinsData.get(),
       .size = jndBinsDataSize}});

  {
    ETNA_PROFILE_GPU(cmd_buf, histogram_binning);

    auto programInfo = etna::get_shader_program("histogram_binning");
    auto set = etna::create_descriptor_set(
      programInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {etna::Binding{
         0, hdr_image.genBinding(sampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
       etna::Binding{1, histData.genBinding()},
       etna::Binding{2, jndBinsData.genBinding()},
       etna::Binding{8, constants.genBinding()}});

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute,
      binningPipeline.getVkPipelineLayout(),
      0,
      {set.getVkSet()},
      {});
    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, binningPipeline.getVkPipeline());

    cmd_buf.dispatch(
      get_linear_wg_count(
        targetPixelCount, HISTOGRAM_WORK_GROUP_SIZE * HISTOGRAM_PIXELS_PER_THREAD),
      1,
      1);
  }

  emit_barriers(
    cmd_buf,
    {vk::BufferMemoryBarrier2{
       .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .srcAccessMask =
         vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
       .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .dstAccessMask =
         vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
       .buffer = histData.get(),
       .size = sizeof(HistogramData)},
     vk::BufferMemoryBarrier2{
       .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .srcAccessMask =
         vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
       .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
       .buffer = jndBinsData.get(),
       .size = jndBinsDataSize}});

  {
    ETNA_PROFILE_GPU(cmd_buf, histogram_distribution);

    auto programInfo = etna::get_shader_program("histogram_distribution");
    auto set = etna::create_descriptor_set(
      programInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {etna::Binding{0, histData.genBinding()}, etna::Binding{8, constants.genBinding()}});

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute,
      distributionPipeline.getVkPipelineLayout(),
      0,
      {set.getVkSet()},
      {});
    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, distributionPipeline.getVkPipeline());

    cmd_buf.dispatch(1, 1, 1);
  }

  emit_barriers(
    cmd_buf,
    {vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask =
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
      .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
      .buffer = histData.get(),
      .size = sizeof(HistogramData)}});
}
