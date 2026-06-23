#define _USE_MATH_DEFINES

#include "WorldRenderer.hpp"

#include <render_components/HistogramEqTonemapper.hpp>
#include <render_components/ReinhardTonemapper.hpp>
#include <render_components/AcesTonemapper.hpp>
#include <render_components/FXAAAntialiaser.hpp>
#include <render_components/FXAA311Antialiaser.hpp>
#include <render_components/TAAAntialiaser.hpp>

#include <render_utils/PostfxRenderer.hpp>
#include <render_utils/Common.hpp>
#include <utils/Common.hpp>
#include <utils/Bitstream.hpp>

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>
#include <etna/Etna.hpp>

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/ext.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>

#include <cassert>
#include <memory>
#include <vector>
#include <random>
#include <ranges>

using def_rng = std::linear_congruential_engine<uint64_t, 16807ul, 0ul, 2147483647ul>;

WorldRenderer::MeshPipeline::MeshPipeline(
  etna::PipelineManager& pipeman,
  const char* prog_name,
  const char* no_prepass_prog_name,
  const char* shadow_prog_name,
  const char* depth_prog_name,
  const etna::GraphicsPipeline::CreateInfo& ci)
{
  auto nci = ci;
  pipelines[size_t(SceneRenderingPass::COLOR)][size_t(DepthFlavour::NORMAL)] =
    pipeman.createGraphicsPipeline(no_prepass_prog_name, nci);
  nci.depthConfig.depthCompareOp = vk::CompareOp::eGreaterOrEqual;
  pipelines[size_t(SceneRenderingPass::COLOR)][size_t(DepthFlavour::REVERSE)] =
    pipeman.createGraphicsPipeline(no_prepass_prog_name, nci);
  nci.depthConfig.depthCompareOp = vk::CompareOp::eEqual;
  pipelines[size_t(SceneRenderingPass::COLOR_AFTER_PREPASS)][size_t(DepthFlavour::NORMAL)] =
    pipeman.createGraphicsPipeline(prog_name, nci);
  pipelines[size_t(SceneRenderingPass::COLOR_AFTER_PREPASS)][size_t(DepthFlavour::REVERSE)] =
    pipeman.createGraphicsPipeline(prog_name, nci);
  programs[size_t(SceneRenderingPass::COLOR)].emplace(
    etna::get_shader_program(no_prepass_prog_name));
  programs[size_t(SceneRenderingPass::COLOR_AFTER_PREPASS)].emplace(
    etna::get_shader_program(prog_name));

  {
    auto wci = ci;
    wci.rasterizationConfig.polygonMode = vk::PolygonMode::eLine;
    pipelines[size_t(SceneRenderingPass::WIRE_COLOR)][size_t(DepthFlavour::NORMAL)] =
      pipeman.createGraphicsPipeline(no_prepass_prog_name, wci);
    wci.depthConfig.depthCompareOp = vk::CompareOp::eGreaterOrEqual;
    pipelines[size_t(SceneRenderingPass::WIRE_COLOR)][size_t(DepthFlavour::REVERSE)] =
      pipeman.createGraphicsPipeline(no_prepass_prog_name, wci);
    wci.depthConfig.depthCompareOp = vk::CompareOp::eEqual;
    pipelines[size_t(SceneRenderingPass::WIRE_COLOR_AFTER_PREPASS)][size_t(DepthFlavour::NORMAL)] =
      pipeman.createGraphicsPipeline(prog_name, wci);
    pipelines[size_t(SceneRenderingPass::WIRE_COLOR_AFTER_PREPASS)][size_t(DepthFlavour::REVERSE)] =
      pipeman.createGraphicsPipeline(prog_name, wci);
    programs[size_t(SceneRenderingPass::WIRE_COLOR)].emplace(
      etna::get_shader_program(no_prepass_prog_name));
    programs[size_t(SceneRenderingPass::WIRE_COLOR_AFTER_PREPASS)].emplace(
      etna::get_shader_program(prog_name));
  }

  {
    auto sci = ci;
    sci.blendingConfig.attachments = {};
    sci.fragmentShaderOutput.colorAttachmentFormats = {};
    sci.fragmentShaderOutput.depthAttachmentFormat = vk::Format::eD16Unorm;
    sci.rasterizationConfig.cullMode = vk::CullModeFlagBits::eBack;
    sci.dynamicStates.push_back(vk::DynamicState::eDepthBias);
    sci.dynamicStates.push_back(vk::DynamicState::eDepthBiasEnable);
    pipelines[size_t(SceneRenderingPass::SHADOW)][size_t(DepthFlavour::NORMAL)] =
      pipeman.createGraphicsPipeline(shadow_prog_name, sci);
    sci.depthConfig.depthCompareOp = vk::CompareOp::eGreaterOrEqual;
    pipelines[size_t(SceneRenderingPass::SHADOW)][size_t(DepthFlavour::REVERSE)] =
      pipeman.createGraphicsPipeline(shadow_prog_name, sci);
    programs[size_t(SceneRenderingPass::SHADOW)].emplace(
      etna::get_shader_program(shadow_prog_name));
  }

  {
    auto sci = ci;
    sci.blendingConfig.attachments = {};
    sci.fragmentShaderOutput.colorAttachmentFormats = {};
    sci.fragmentShaderOutput.depthAttachmentFormat = vk::Format::eD16Unorm;
    sci.rasterizationConfig.cullMode = vk::CullModeFlagBits::eFront;
    sci.dynamicStates.push_back(vk::DynamicState::eDepthBias);
    sci.dynamicStates.push_back(vk::DynamicState::eDepthBiasEnable);
    pipelines[size_t(SceneRenderingPass::SHADOW_FRONT_CULLED)][size_t(DepthFlavour::NORMAL)] =
      pipeman.createGraphicsPipeline(shadow_prog_name, sci);
    sci.depthConfig.depthCompareOp = vk::CompareOp::eGreaterOrEqual;
    pipelines[size_t(SceneRenderingPass::SHADOW_FRONT_CULLED)][size_t(DepthFlavour::REVERSE)] =
      pipeman.createGraphicsPipeline(shadow_prog_name, sci);
    programs[size_t(SceneRenderingPass::SHADOW_FRONT_CULLED)].emplace(
      etna::get_shader_program(shadow_prog_name));
  }

  {
    auto dci = ci;
    dci.blendingConfig.attachments = {};
    dci.fragmentShaderOutput.colorAttachmentFormats = {};
    pipelines[size_t(SceneRenderingPass::DEPTH_PREPASS)][size_t(DepthFlavour::NORMAL)] =
      pipeman.createGraphicsPipeline(depth_prog_name, dci);
    dci.depthConfig.depthCompareOp = vk::CompareOp::eGreaterOrEqual;
    pipelines[size_t(SceneRenderingPass::DEPTH_PREPASS)][size_t(DepthFlavour::REVERSE)] =
      pipeman.createGraphicsPipeline(depth_prog_name, dci);
    programs[size_t(SceneRenderingPass::DEPTH_PREPASS)].emplace(
      etna::get_shader_program(depth_prog_name));
    dci.rasterizationConfig.polygonMode = vk::PolygonMode::eLine;
    pipelines[size_t(SceneRenderingPass::WIRE_DEPTH_PREPASS)][size_t(DepthFlavour::NORMAL)] =
      pipeman.createGraphicsPipeline(depth_prog_name, dci);
    dci.depthConfig.depthCompareOp = vk::CompareOp::eGreaterOrEqual;
    pipelines[size_t(SceneRenderingPass::WIRE_DEPTH_PREPASS)][size_t(DepthFlavour::REVERSE)] =
      pipeman.createGraphicsPipeline(depth_prog_name, dci);
    programs[size_t(SceneRenderingPass::WIRE_DEPTH_PREPASS)].emplace(
      etna::get_shader_program(depth_prog_name));
  }
}

WorldRenderer::WorldRenderer(const etna::GpuWorkCount& wc, const Config& config)
  : sceneMgr{std::make_unique<SceneManager>(wc)}
  , wc{wc}
  , cfg{config}
  , ssaoBuffer{DoubleBufferedImage::CreateInfo{&wc}}
  , motionVectors{DoubleBufferedImage::CreateInfo{&wc}}
  , taaTarget{DoubleBufferedImage::CreateInfo{&wc}}
{
  registerViewContextManager();
  registerSrgbEncoder();

  registerTonemapper<HistogramEqTonemapper>(TonemappingTechnique::HISTOGRAM_EQ);
  registerTonemapper<ReinhardTonemapper>(TonemappingTechnique::REINHARD);
  registerTonemapper<AcesTonemapper>(TonemappingTechnique::ACES);

  registerAntialiaser<FXAAAntialiaser>(AATechnique::FXAA);
  registerAntialiaser<FXAA311Antialiaser>(AATechnique::FXAA311);
  registerAntialiaser<TAAAntialiaser>(
    AATechnique::TAA,
    TAAAntialiaser::CreateInfo{
      .cb = {
        .curFrameProvider =
          [this] {
            const auto& tgt = taaTarget.curBuf();
            return std::make_pair(tgt.get(), tgt.getView({}));
          },
        .prevFrameProvider =
          [this] {
            return taaTarget.prevBuf().genBinding(
              defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal);
          },
        .motionVectorsProvider =
          [this] {
            return motionVectors.curBuf().genBinding(
              defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal);
          },
        .depthProvider =
          [this] {
            return mainViewDepth.genBinding(
              defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal);
          },
        .viewParamsProvider = [this] { return mainViewContext->viewParamsBuf.get().genBinding(); },
      }});

  generateSsaoKernel(std::span{constantsData.ssaoData.ssaoKernel, ssaoTotalLimitSamples});
  generateSsaoKernelRotations(constantsData.ssaoData.ssaoKernelRotations);
  generateTaaJitterSequence(taaJitterSequence);

  if (cfg.useDebugConfig)
    loadDebugConfig();
}

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  // @TODO: tighter format
  createManagedImage(
    hdrTarget,
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "hdr_target",
      .format = vk::Format::eR32G32B32A32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});

  createManagedImage(
    mainViewDepth,
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "main_view_depth",
      .format = vk::Format::eD32Sfloat,
      .imageUsage =
        vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled});

  // @TODO: compact gbuffer
  createManagedImage(
    gbufAlbedo,
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "gbuffer_albedo",
      .format = vk::Format::eR32G32B32A32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});
  createManagedImage(
    gbufMaterial,
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "gbuffer_material",
      .format = vk::Format::eR32G32B32A32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});
  createManagedImage(
    gbufNormal,
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "gbuffer_normal",
      .format = vk::Format::eR32G32B32A32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});
  createManagedImage(
    gbufTransmission,
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "gbuffer_transmission",
      .format = vk::Format::eR32G32B32A32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});

  createManagedImage(
    motionVectors.getRawBuf(0),
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "motion_vectors0",
      .format = vk::Format::eR32G32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});
  createManagedImage(
    motionVectors.getRawBuf(1),
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "motion_vectors1",
      .format = vk::Format::eR32G32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});

  defaultSampler = etna::Sampler(etna::Sampler::CreateInfo{
    .name = "default_sampler", .minLod = 0.f, .maxLod = VK_LOD_CLAMP_NONE});
  defaultMirrorSampler = etna::Sampler(etna::Sampler::CreateInfo{
    .addressMode = vk::SamplerAddressMode::eMirroredRepeat,
    .name = "default_mirror_sampler",
    .minLod = 0.f,
    .maxLod = VK_LOD_CLAMP_NONE});

  constants.emplace(wc, [](size_t) {
    return create_buffer(etna::Buffer::CreateInfo{
      .size = sizeof(constantsData),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
      .name = "constants"});
  });
  lights.emplace(wc, [](size_t) {
    return create_buffer(etna::Buffer::CreateInfo{
      .size = sizeof(sceneMgr->getLights()),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
      .name = "lights"});
  });
  constants->iterate([](auto& buf) { buf.map(); });
  lights->iterate([](auto& buf) { buf.map(); });
  prevLights = {};

  lightMatricesBuf = create_buffer(etna::Buffer::CreateInfo{
    .size = sizeof(LightMatrices),
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "light_matrices"});

  stubUniBuffer = create_buffer(etna::Buffer::CreateInfo{
    .size = 16,
    .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
    .name = "stub_uniform"});
  stubStorageBuffer = create_buffer(etna::Buffer::CreateInfo{
    .size = 16,
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "stub_storage"});

  createManagedImage(
    ssaoBuffer.curBuf(),
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "ssao_buffer0",
      .format = vk::Format::eR32G32B32A32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});
  createManagedImage(
    ssaoBuffer.prevBuf(),
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "ssao_buffer1",
      .format = vk::Format::eR32G32B32A32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});
  createManagedImage(
    ssaoBlurredBuffer,
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "ssao_blurred_buffer",
      .format = vk::Format::eR32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});

  createManagedImage(
    taaTarget.getRawBuf(0),
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "taa_target0",
      .format = vk::Format::eR32G32B32A32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});
  createManagedImage(
    taaTarget.getRawBuf(1),
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "taa_target1",
      .format = vk::Format::eR32G32B32A32Sfloat,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});

  for (auto& component : rcomponents)
    component->allocateResources(resolution);
}

void WorldRenderer::loadScene(std::filesystem::path path)
{
  // @TODO: make recallable, i.e. implement cleanup

  sceneMgr->selectScene(
    path,
    {!cfg.disablePointLightsShadowsFeature,
     !cfg.disableSpotLightsShadowsFeature,
     !cfg.disableDirectionalLightsShadowsFeature},
    cfg.testMultiplexScene ? cfg.testMultiplexing : SceneMultiplexing{});

  mainViewContext.emplace(viewCtxMgr->alloc("main"));

  if (!cfg.disablePointLightsShadowsFeature)
  {
    pointLightViews.reserve(sceneMgr->getLights().pointLightsCount);
    for (size_t i = 0; i < sceneMgr->getLights().pointLightsCount; ++i)
    {
      pointLightViews.emplace_back(array_make<ViewContext, 6>(
        [this, i] { return viewCtxMgr->alloc(fmt::format("point{}", i).c_str()); }));
    }
  }
  if (!cfg.disableSpotLightsShadowsFeature)
  {
    spotLightViews.reserve(sceneMgr->getLights().spotLightsCount);
    for (size_t i = 0; i < sceneMgr->getLights().spotLightsCount; ++i)
    {
      spotLightViews.emplace_back(viewCtxMgr->alloc(fmt::format("spot{}", i).c_str()));
    }
  }
  if (!cfg.disableDirectionalLightsShadowsFeature)
  {
    directionalLightCascadeViews.reserve(sceneMgr->getLights().directionalLightsCount);
    for (size_t i = 0; i < sceneMgr->getLights().directionalLightsCount; ++i)
    {
      directionalLightCascadeViews.emplace_back(array_make<ViewContext, CSM_CASCADE_COUNT>(
        [this, i] { return viewCtxMgr->alloc(fmt::format("dir{}", i).c_str()); }));
    }
  }

  if (sceneMgr->hasTerrain())
  {
    spdlog::info("JB_terrain: terrain loaded!");

    terrain.emplace(TerrainRenderingData{});

    memcpy(&terrain->sourceData, &sceneMgr->getTerrainData(), sizeof(sceneMgr->getTerrainData()));
    terrain->source = create_buffer(etna::Buffer::CreateInfo{
      .size = sizeof(sceneMgr->getTerrainData()),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
      .name = "terrain_data"});

    memcpy(terrain->source.map(), &terrain->sourceData, sizeof(terrain->sourceData));

    createManagedImage(
      terrain->geometryClipmap,
      etna::Image::CreateInfo{
        .extent = vk::Extent3D{CLIPMAP_RESOLUTION, CLIPMAP_RESOLUTION, 1},
        .name = "geometry_clipmap",
        .format = vk::Format::eR32Sfloat,
        .imageUsage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
        .layers = CLIPMAP_LEVEL_COUNT});
    createManagedImage(
      terrain->normalClipmap,
      etna::Image::CreateInfo{
        .extent = vk::Extent3D{CLIPMAP_RESOLUTION, CLIPMAP_RESOLUTION, 1},
        .name = "normal_clipmap",
        .format = vk::Format::eR32G32B32A32Sfloat,
        .imageUsage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
        .layers = CLIPMAP_LEVEL_COUNT});
    createManagedImage(
      terrain->albedoClipmap,
      etna::Image::CreateInfo{
        .extent = vk::Extent3D{CLIPMAP_RESOLUTION, CLIPMAP_RESOLUTION, 1},
        .name = "albedo_clipmap",
        .format = vk::Format::eR32G32B32A32Sfloat,
        .imageUsage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
        .layers = CLIPMAP_LEVEL_COUNT});
    createManagedImage(
      terrain->matdataClipmap,
      etna::Image::CreateInfo{
        .extent = vk::Extent3D{CLIPMAP_RESOLUTION, CLIPMAP_RESOLUTION, 1},
        .name = "matdata_clipmap",
        .format = vk::Format::eR32G32B32A32Sfloat,
        .imageUsage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled,
        .layers = CLIPMAP_LEVEL_COUNT});

    terrain->clipmapSampler = etna::Sampler(etna::Sampler::CreateInfo{
      .filter = vk::Filter::eLinear,
      .addressMode = vk::SamplerAddressMode::eRepeat,
      .name = "terrain_clipmap_sampler"});

    for (size_t i = 0; i < CLIPMAP_LEVEL_COUNT; ++i)
    {
      terrain->geometryLevelsBindings.emplace_back(
        0,
        terrain->geometryClipmap.genBinding(
          {}, vk::ImageLayout::eGeneral, {.baseLayer = uint32_t(i), .layerCount = 1}),
        uint32_t(i));
      terrain->normalLevelsBindings.emplace_back(
        1,
        terrain->normalClipmap.genBinding(
          {}, vk::ImageLayout::eGeneral, {.baseLayer = uint32_t(i), .layerCount = 1}),
        uint32_t(i));
      terrain->albedoLevelsBindings.emplace_back(
        2,
        terrain->albedoClipmap.genBinding(
          {}, vk::ImageLayout::eGeneral, {.baseLayer = uint32_t(i), .layerCount = 1}),
        uint32_t(i));
      terrain->matdataLevelsBindings.emplace_back(
        3,
        terrain->matdataClipmap.genBinding(
          {}, vk::ImageLayout::eGeneral, {.baseLayer = uint32_t(i), .layerCount = 1}),
        uint32_t(i));
      terrain->geometryLevelsSamplerBindings.emplace_back(
        2,
        terrain->geometryClipmap.genBinding(
          terrain->clipmapSampler.get(),
          vk::ImageLayout::eShaderReadOnlyOptimal,
          {.baseLayer = uint32_t(i), .layerCount = 1}),
        uint32_t(i));
      terrain->normalLevelsSamplerBindings.emplace_back(
        3,
        terrain->normalClipmap.genBinding(
          terrain->clipmapSampler.get(),
          vk::ImageLayout::eShaderReadOnlyOptimal,
          {.baseLayer = uint32_t(i), .layerCount = 1}),
        uint32_t(i));
      terrain->albedoLevelsSamplerBindings.emplace_back(
        4,
        terrain->albedoClipmap.genBinding(
          terrain->clipmapSampler.get(),
          vk::ImageLayout::eShaderReadOnlyOptimal,
          {.baseLayer = uint32_t(i), .layerCount = 1}),
        uint32_t(i));
      terrain->matdataLevelsSamplerBindings.emplace_back(
        5,
        terrain->matdataClipmap.genBinding(
          terrain->clipmapSampler.get(),
          vk::ImageLayout::eShaderReadOnlyOptimal,
          {.baseLayer = uint32_t(i), .layerCount = 1}),
        uint32_t(i));
    }

    terrain->chunkHeightBoundsBuf = create_buffer(etna::Buffer::CreateInfo{
      .size = sizeof(ChunkHeightBoundsData),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "terrain_chunk_height_bounds"});

    queueClipmapInvalidation();
  }
  else
  {
    spdlog::info("JB_terrain: terrain not present");
  }

  if (sceneMgr->hasVegetation())
  {
    const auto& td = sceneMgr->getTerrainData();
    spdlog::info("JB_terrain: loaded {} vegetation types!", td.vegetationTypeCount);

    vegetation.emplace(VegetationRenderingData{});

    vegetation->culledChunkBuffer = create_buffer(etna::Buffer::CreateInfo{
      .size = vegChunkBufferSizeBytes(),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "vegetation_chunk_buffer"});
    vegetation->indirectDispatchBuffer = create_buffer(etna::Buffer::CreateInfo{
      .size = terrain->sourceData.detailCount * sizeof(IndirectDispatchCommand),
      .bufferUsage =
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "vegetation_indirect_dispatch_buffer"});
    vegetation->grassInstancesBuffer = create_buffer(etna::Buffer::CreateInfo{
      .size = vegInstBufferSizeBytes(),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "vegetation_instances_buffer"});
  }
  else
  {
    spdlog::info("JB_terrain: vegetation not present");
  }

  if (sceneMgr->hasSkybox())
  {
    spdlog::info("JB_skybox: skybox loaded!");

    skybox.emplace(SkyboxRenderingData{});

    memcpy(&skybox->sourceData, &sceneMgr->getSkyboxData(), sizeof(sceneMgr->getSkyboxData()));
    skybox->source = create_buffer(etna::Buffer::CreateInfo{
      .size = sizeof(sceneMgr->getSkyboxData()),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
      .name = "skybox_data"});

    memcpy(skybox->source.map(), &skybox->sourceData, sizeof(skybox->sourceData));
  }
  else
  {
    spdlog::info("JB_skybox: skybox not present");
  }

  if (sceneMgr->hasWater())
  {
    spdlog::info("JB_water: water loaded!");

    water.emplace(WaterRenderingData{});

    memcpy(&water->sourceData, &sceneMgr->getWaterData(), sizeof(sceneMgr->getWaterData()));
    water->source = create_buffer(etna::Buffer::CreateInfo{
      .size = sizeof(sceneMgr->getWaterData()),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
      .name = "water_data"});
    memcpy(water->source.map(), &water->sourceData, sizeof(water->sourceData));

    for (size_t i = 0; auto& cascade : water->cascades)
    {
      createManagedImage(
        cascade.wavevectorFrequencyTex,
        etna::Image::CreateInfo{
          .extent = vk::Extent3D{WATER_CASCADE_RES, WATER_CASCADE_RES, 1},
          .name = fmt::format("wavevector_freq_{}", i),
          .format = vk::Format::eR32G32B32A32Sfloat,
          .imageUsage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled});
      createManagedImage(
        cascade.timeIndepSpectraTex,
        etna::Image::CreateInfo{
          .extent = vk::Extent3D{WATER_CASCADE_RES, WATER_CASCADE_RES, 1},
          .name = fmt::format("TI_spectra_{}", i),
          .format = vk::Format::eR32G32B32A32Sfloat,
          .imageUsage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled});
      createManagedImage(
        cascade.spatialDisplacementAndDxzTex,
        etna::Image::CreateInfo{
          .extent = vk::Extent3D{WATER_CASCADE_RES, WATER_CASCADE_RES, 1},
          .name = fmt::format("wave_disp_and_dxz_{}", i),
          .format = vk::Format::eR32G32B32A32Sfloat,
          .imageUsage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled});
      createManagedImage(
        cascade.spatialDisplacementOtherDerivativesTex,
        etna::Image::CreateInfo{
          .extent = vk::Extent3D{WATER_CASCADE_RES, WATER_CASCADE_RES, 1},
          .name = fmt::format("wave_disp_other_derivatives_{}", i),
          .format = vk::Format::eR32G32B32A32Sfloat,
          .imageUsage = vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled});
      ++i;
    }
  }
  else
  {
    spdlog::info("JB_skybox: skybox not present");
  }

  auto fragProgInfo = etna::get_shader_program("static_mesh");
  auto compProgInfo = etna::get_shader_program("clipmap_gen");

  materialParamsDsetFrag = etna::create_persistent_descriptor_set(
    fragProgInfo.getDescriptorLayoutId(1),
    {etna::Binding{0, sceneMgr->getMaterialParamsBuf().genBinding()}});
  materialParamsDsetComp = etna::create_persistent_descriptor_set(
    compProgInfo.getDescriptorLayoutId(1),
    {etna::Binding{0, sceneMgr->getMaterialParamsBuf().genBinding()}});

  // @TODO: pull out
  std::vector<etna::Binding> texBindings, smpBindings;
  texBindings.reserve(sceneMgr->getTextures().size());
  smpBindings.reserve(sceneMgr->getSamplers().size());

  for (size_t i = 0; i < sceneMgr->getTextures().size(); ++i)
  {
    const auto& tex = sceneMgr->getTex(TexId(i));
    etna::Image::ViewParams vps{};
    if (tex.getCreationFlags() & vk::ImageCreateFlagBits::eCubeCompatible)
      vps.type = vk::ImageViewType::eCube;
    texBindings.emplace_back(etna::Binding{
      0, tex.genBinding({}, vk::ImageLayout::eShaderReadOnlyOptimal, vps), uint32_t(i)});
    registerManagedImage(tex, fmt::format("bindless_tex_{}[{}]", i, tex.getName()));
  }
  for (size_t i = 0; i < sceneMgr->getSamplers().size(); ++i)
  {
    const auto& smp = sceneMgr->getSmp(SmpId(i));
    smpBindings.emplace_back(etna::Binding{0, smp.genBinding(), uint32_t(i)});
  }

  bindlessTexturesDsetFrag =
    etna::create_persistent_descriptor_set(fragProgInfo.getDescriptorLayoutId(2), texBindings);
  bindlessTexturesDsetComp = etna::create_persistent_descriptor_set(
    compProgInfo.getDescriptorLayoutId(2), std::move(texBindings));

  bindlessSamplersDsetFrag =
    etna::create_persistent_descriptor_set(fragProgInfo.getDescriptorLayoutId(3), smpBindings);
  bindlessSamplersDsetComp = etna::create_persistent_descriptor_set(
    compProgInfo.getDescriptorLayoutId(3), std::move(smpBindings));
}

void WorldRenderer::loadShaders()
{
  etna::create_program(
    "static_mesh",
    {RENDERER_SHADERS_ROOT "static_mesh.frag.spv", RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program("static_mesh_depth", {RENDERER_SHADERS_ROOT "static_mesh_depth.vert.spv"});
  etna::create_program(
    "terrain_mesh",
    {RENDERER_SHADERS_ROOT "terrain_mesh.frag.spv",
     RENDERER_SHADERS_ROOT "terrain_mesh.vert.spv",
     RENDERER_SHADERS_ROOT "terrain_mesh.tesc.spv",
     RENDERER_SHADERS_ROOT "terrain_mesh.tese.spv"});
  etna::create_program(
    "terrain_mesh_depth",
    {RENDERER_SHADERS_ROOT "terrain_mesh.vert.spv",
     RENDERER_SHADERS_ROOT "terrain_mesh.tesc.spv",
     RENDERER_SHADERS_ROOT "terrain_mesh_depth.tese.spv"});
  etna::create_program("clipmap_gen", {RENDERER_SHADERS_ROOT "clipmap_gen.comp.spv"});
  etna::create_program(
    "reset_terrain_chunk_height_bounds",
    {RENDERER_SHADERS_ROOT "reset_terrain_chunk_height_bounds.comp.spv"});
  etna::create_program(
    "generate_terrain_chunk_height_bounds",
    {RENDERER_SHADERS_ROOT "generate_terrain_chunk_height_bounds.comp.spv"});
  etna::create_program(
    "transfer_terrain_chunk_height_bounds",
    {RENDERER_SHADERS_ROOT "transfer_terrain_chunk_height_bounds.comp.spv"});
  etna::create_program(
    "transfer_light_mats", {RENDERER_SHADERS_ROOT "transfer_light_mats.comp.spv"});
  etna::create_program(
    "grass_mesh",
    {RENDERER_SHADERS_ROOT "grass_mesh.frag.spv", RENDERER_SHADERS_ROOT "grass_mesh.vert.spv"});
  etna::create_program(
    "grass_mesh_no_prepass",
    {RENDERER_SHADERS_ROOT "grass_mesh_no_prepass.frag.spv",
     RENDERER_SHADERS_ROOT "grass_mesh.vert.spv"});
  etna::create_program(
    "grass_mesh_depth",
    {RENDERER_SHADERS_ROOT "grass_mesh_depth.frag.spv",
     RENDERER_SHADERS_ROOT "grass_mesh_depth.vert.spv"});
  etna::create_program(
    "grass_generate_clear_chunks", {RENDERER_SHADERS_ROOT "grass_generate_clear_chunks.comp.spv"});
  etna::create_program(
    "grass_generate_cull_chunks", {RENDERER_SHADERS_ROOT "grass_generate_cull_chunks.comp.spv"});
  etna::create_program(
    "grass_generate_sort_chunks", {RENDERER_SHADERS_ROOT "grass_generate_sort_chunks.comp.spv"});
  etna::create_program(
    "grass_generate_prepare_inst_command",
    {RENDERER_SHADERS_ROOT "grass_generate_prepare_inst_command.comp.spv"});
  etna::create_program(
    "grass_generate_instances", {RENDERER_SHADERS_ROOT "grass_generate_instances.comp.spv"});
  etna::create_program(
    "water_time_indep_spectra_gen",
    {RENDERER_SHADERS_ROOT "water_time_indep_spectra_gen.comp.spv"});
  etna::create_program(
    "water_time_indep_spectra_conjugate",
    {RENDERER_SHADERS_ROOT "water_time_indep_spectra_conjugate.comp.spv"});
  etna::create_program(
    "water_time_spectra_gen", {RENDERER_SHADERS_ROOT "water_time_spectra_gen.comp.spv"});

  for (auto& component : rcomponents)
    component->loadShaders();
}

void WorldRenderer::setupPipelines(vk::Format swapchain_format)
{
  // @TODO: not this dumb (problem -- tonemappers can write to either or. Maybe better always
  // ldr?)
  createManagedImage(
    ldrTarget,
    etna::Image::CreateInfo{
      .extent = vk::Extent3D{resolution.x, resolution.y, 1},
      .name = "ldr_target",
      .format = swapchain_format,
      .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});

  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = {etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = sceneMgr->getVertexFormatDescription(),
    }},
  };

  auto& pipelineManager = etna::get_context().getPipelineManager();

  // @TODO: compactify
  auto
    meshPipelineCreateInfo =
      etna::GraphicsPipeline::CreateInfo{
        .vertexShaderInput = sceneVertexInputDesc,
        .rasterizationConfig =
          vk::PipelineRasterizationStateCreateInfo{
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eCounterClockwise,
            .lineWidth = 1.f,
          },
        .blendingConfig =
          {.attachments =
             {
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
             },
           .logicOp = vk::LogicOp::eSet},
        .fragmentShaderOutput =
          {
            .colorAttachmentFormats = // @TODO: save these into vars
            {vk::Format::eR32G32B32A32Sfloat,
             vk::Format::eR32G32B32A32Sfloat,
             vk::Format::eR32G32B32A32Sfloat,
             vk::Format::eR32G32B32A32Sfloat,
             vk::Format::eR32G32Sfloat},
            .depthAttachmentFormat = vk::Format::eD32Sfloat,
          },
      };
  auto
    terrainPipelineCreateInfo =
      etna::GraphicsPipeline::CreateInfo{
        .inputAssemblyConfig = {.topology = vk::PrimitiveTopology::ePatchList},
        .tessellationConfig = {.patchControlPoints = 4},
        .rasterizationConfig =
          vk::PipelineRasterizationStateCreateInfo{
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eBack,
            .frontFace = vk::FrontFace::eCounterClockwise,
            .lineWidth = 1.f,
          },
        .blendingConfig =
          {.attachments =
             {
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
             },
           .logicOp = vk::LogicOp::eSet},
        .fragmentShaderOutput =
          {
            .colorAttachmentFormats = // @TODO: save these into vars
            {vk::Format::eR32G32B32A32Sfloat,
             vk::Format::eR32G32B32A32Sfloat,
             vk::Format::eR32G32B32A32Sfloat,
             vk::Format::eR32G32B32A32Sfloat,
             vk::Format::eR32G32Sfloat},
            .depthAttachmentFormat = vk::Format::eD32Sfloat,
          },
      };
  auto
    vegetationPipelineCreateInfo =
      etna::GraphicsPipeline::CreateInfo{
        .rasterizationConfig =
          vk::PipelineRasterizationStateCreateInfo{
            .polygonMode = vk::PolygonMode::eFill,
            .cullMode = vk::CullModeFlagBits::eNone,
            .frontFace = vk::FrontFace::eCounterClockwise,
            .lineWidth = 1.f,
          },
        .blendingConfig =
          {.attachments =
             {
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
               vk::PipelineColorBlendAttachmentState{
                 .blendEnable = vk::False,
                 .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
                   vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
               },
             },
           .logicOp = vk::LogicOp::eSet},
        .fragmentShaderOutput =
          {
            .colorAttachmentFormats = // @TODO: save these into vars
            {vk::Format::eR32G32B32A32Sfloat,
             vk::Format::eR32G32B32A32Sfloat,
             vk::Format::eR32G32B32A32Sfloat,
             vk::Format::eR32G32B32A32Sfloat,
             vk::Format::eR32G32Sfloat},
            .depthAttachmentFormat = vk::Format::eD32Sfloat,
          },
      };

  staticMeshPipeline.emplace(
    pipelineManager,
    "static_mesh",
    "static_mesh",
    "static_mesh_depth",
    "static_mesh_depth",
    meshPipelineCreateInfo);
  terrainMeshPipeline.emplace(
    pipelineManager,
    "terrain_mesh",
    "terrain_mesh",
    "terrain_mesh_depth",
    "terrain_mesh_depth",
    terrainPipelineCreateInfo);
  vegetationMeshPipeline.emplace(
    pipelineManager,
    "grass_mesh",
    "grass_mesh_no_prepass",
    "grass_mesh_depth",
    "grass_mesh_depth",
    vegetationPipelineCreateInfo);

  generateClipmapPipeline = pipelineManager.createComputePipeline("clipmap_gen", {});
  resetTerrainChunkHeightBoundsPipeline =
    pipelineManager.createComputePipeline("reset_terrain_chunk_height_bounds", {});
  generateTerrainChunkHeightBoundsPipeline =
    pipelineManager.createComputePipeline("generate_terrain_chunk_height_bounds", {});
  transferTerrainChunkHeightBoundsPipeline =
    pipelineManager.createComputePipeline("transfer_terrain_chunk_height_bounds", {});
  transferLightMatsPipeline = pipelineManager.createComputePipeline("transfer_light_mats", {});
  vegetationGenerateClearChunks =
    pipelineManager.createComputePipeline("grass_generate_clear_chunks", {});
  vegetationGenerateCullChunks =
    pipelineManager.createComputePipeline("grass_generate_cull_chunks", {});
  vegetationGenerateSortChunks =
    pipelineManager.createComputePipeline("grass_generate_sort_chunks", {});
  vegetationGeneratePrepareInstCommand =
    pipelineManager.createComputePipeline("grass_generate_prepare_inst_command", {});
  vegetationGenerateInstances =
    pipelineManager.createComputePipeline("grass_generate_instances", {});

  waterInitalSpectraGenerate =
    pipelineManager.createComputePipeline("water_time_indep_spectra_gen", {});
  waterInitalSpectraConjugate =
    pipelineManager.createComputePipeline("water_time_indep_spectra_conjugate", {});
  waterTimeSpectraGenerate = pipelineManager.createComputePipeline("water_time_spectra_gen", {});

  gbufferResolver = std::make_unique<PostfxRenderer>(PostfxRenderer::CreateInfo{
    "gbuffer_resolve",
    RENDERER_SHADERS_ROOT "gbuffer_resolve.frag.spv",
    vk::Format::eR32G32B32A32Sfloat,
    {resolution.x, resolution.y}});

  ssaoGen = std::make_unique<PostfxRenderer>(PostfxRenderer::CreateInfo{
    "ssao_generate",
    RENDERER_SHADERS_ROOT "ssao_generate.frag.spv",
    vk::Format::eR32G32B32A32Sfloat,
    {resolution.x, resolution.y}});
  ssaoBlur = std::make_unique<PostfxRenderer>(PostfxRenderer::CreateInfo{
    "ssao_blur",
    RENDERER_SHADERS_ROOT "ssao_blur.frag.spv",
    vk::Format::eR32Sfloat,
    {resolution.x, resolution.y}});

  bboxRenderer = std::make_unique<BboxRenderer>(BboxRenderer::CreateInfo{swapchain_format});
  quadRenderer = std::make_unique<QuadRenderer>(QuadRenderer::CreateInfo{swapchain_format});

  for (auto& component : rcomponents)
    component->setupPipelines(swapchain_format, debugDrawers);
}

void WorldRenderer::onShadersReloaded()
{
  queueClipmapInvalidation();
  needRegenSsaoKernel = true;
  pointLightsSettingsDirty = true;
  spotLightsSettingsDirty = true;
  directionalLightsSettingsDirty = true;
  waterSettingsDirty = true;
}

void WorldRenderer::debugInput(const Keyboard&, const Mouse&, bool mouse_captured)
{
  settingsGuiEnabled = !mouse_captured;
}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;

  {
    dt = prevTime >= 0.f ? (packet.currentTime - prevTime) : 0.f;
    prevTime = packet.currentTime;

    smoothedDt.addSample(dt);

    ++frame;
  }

  {
    mainCam = packet.mainCam;
  }

  {
    constantsData.playerWorldPos = packet.mainCam.position;
    if (terrain)
    {
      constantsData.terrainFirstInstance = sceneMgr->getTerrainIndirectCommands()[0].firstInstance;

      if (terrain->invalidateClipmapRequested)
      {
        constantsData.toroidalUpdatePlayerWorldPos = snap_to_toroidal_update_grid(
          XZ(constantsData.playerWorldPos) - glm::vec2{CLIPMAP_LEVEL_WSIZE(CLIPMAP_LEVEL_COUNT)});
        terrain->invalidateClipmapRequested = false;
      }

      const auto toroidalOffsetRaw =
        XZ(constantsData.playerWorldPos) - constantsData.toroidalUpdatePlayerWorldPos;
      const float disp = std::max(glm::abs(toroidalOffsetRaw.x), glm::abs(toroidalOffsetRaw.y));
      if (disp >= CLIMPAP_UPDATE_GRID_SIZE)
      {
        const auto oldToroidalUpdatePos = std::exchange(
          constantsData.toroidalUpdatePlayerWorldPos,
          snap_to_toroidal_update_grid(
            constantsData.toroidalUpdatePlayerWorldPos + toroidalOffsetRaw));
        constantsData.toroidalOffset =
          constantsData.toroidalUpdatePlayerWorldPos - oldToroidalUpdatePos;

        terrain->needToroidalUpdate = true;
      }
    }
  }

  if (needRegenSsaoKernel)
  {
    generateSsaoKernel(std::span{constantsData.ssaoData.ssaoKernel, ssaoTotalLimitSamples});
    generateSsaoKernelRotations(constantsData.ssaoData.ssaoKernelRotations);
    constantsData.ssaoForceDropHistory = true;
    needRegenSsaoKernel = false;
  }
  else
  {
    constantsData.ssaoForceDropHistory = false;
  }

  if (cfg.disablePointLightsShadowsFeature)
    pointLightShadowsSettings.enable = false;
  if (cfg.disableSpotLightsShadowsFeature)
    spotLightShadowsSettings.enable = false;
  if (cfg.disableDirectionalLightsShadowsFeature)
    directionalLightShadowsSettings.enable = false;

  {
    constantsData.cullingMode = doSatCulling ? CullingMode::SAT : CullingMode::PER_VERTEX;

    constantsData.useSkybox = skybox.has_value() && enableSkybox;
    constantsData.drawTerrainSplattedDetail = terrain.has_value() && drawTerrainSplattedDetail;

    constantsData.usePointLightShadows = pointLightShadowsSettings.enable;
    constantsData.useSpotLightShadows = spotLightShadowsSettings.enable;
    constantsData.useDirectionalLightShadows = directionalLightShadowsSettings.enable;
    constantsData.pointLightShadowsTechnique = pointLightShadowsSettings.technique;
    constantsData.spotLightShadowsTechnique = spotLightShadowsSettings.technique;
    constantsData.directionalLightShadowsTechnique = directionalLightShadowsSettings.technique;

    constantsData.drawCascadesInSolidColor = drawCascadesInSolidColor;

    constantsData.terrainNoiseRelHeightAmp = terrainNoiseRelHeightAmp;
    constantsData.terrainNoisePeriod = terrainNoisePeriod;

    constantsData.vegetationRenderingDistance = vegetationRenderingDistance;
    constantsData.vegetationRenderingDropoffDistance = vegetationRenderingDropoffDistance;

    constantsData.windDirection = windDirection;
    constantsData.windStrength = windStrength;

    constantsData.useTonemapping = doTonemapping;
    constantsData.useSharedMemForTonemapping = useSharedMemForTonemapping;
    constantsData.histEqTonemappingRegW = histEqTonemappingRegW;
    constantsData.histEqTonemappingRefinedW = histEqTonemappingRefinedW;
    constantsData.histEqTonemappingMinAdmissibleLum = histEqTonemappingMinAdmissibleLum;
    constantsData.histEqTonemappingMaxAdmissibleLum = histEqTonemappingMaxAdmissibleLum;
    constantsData.acesExposure = acesExposure;

    constantsData.csmSplitLambda = csmSplitLambda;
    constantsData.csmBlendingBeltSize = csmBlendingBeltSize;

    constantsData.dt = dt;
    constantsData.time = packet.currentTime;
    constantsData.frameNo = shader_uint(frame);
    constantsData.mainTargetResolution = resolution;
    constantsData.mainTargetInverseResolution = 1.f / glm::vec2(resolution);

    constantsData.useSsao = useSsao;

    constantsData.ambientLightCoeff = ambientCoeff;
    constantsData.useSkyboxForAmbient = useSkyboxForAmbient;

    constantsData.ssaoRadius = ssaoKernelRadius;
    constantsData.ssaoBias = ssaoBias;
    constantsData.ssaoPower = ssaoPower;
    constantsData.ssaoLimitSamples = ssaoTotalLimitSamples;

    constantsData.ssaoDoTemporalAccum = ssaoTemporalAccumBacklog > 1;
    constantsData.ssaoTemporalAccumBacklog = ssaoTemporalAccumBacklog;
    constantsData.ssaoTemporalAccumBacklog = ssaoTemporalAccumBacklog;
    constantsData.ssaoEmaCoeff = ssaoEmaCoeff;
    constantsData.ssaoDepthRejectionThreshold = ssaoDepthRejectionThreshold;

    constantsData.ssaoConservariveTemporalCaching = ssaoConservariveTemporalCaching;

    constantsData.gammaEncodeInTonemapping = !useAA ||
      ((currentAATechnique == AATechnique::FXAA || currentAATechnique == AATechnique::FXAA311) &&
       fxaaAntialiasInSrgb);
    constantsData.fxaaAntialiasInSrgb = fxaaAntialiasInSrgb;

    constantsData.taaEmaCoeff = taaEmaCoeff;

    constantsData.waterF = waterSettings.f;
    constantsData.waterH = waterSettings.h;
    constantsData.waterG = waterSettings.g;
    constantsData.waterRho = waterSettings.rho;
    constantsData.waterSurfaceTension = waterSettings.surfaceTension;
    constantsData.waterWindUnitsToMps = waterSettings.windUnitsToMps;
    constantsData.waterEnabled = waterSettings.enable;
  }

  {
    constantsData.blueNoiseTexSmp = sceneMgr->getBlueNoiseTexSmp();
  }

  mainViewParams = view_params_for_cam(
    mainCam,
    aspect(),
    false,
    true,
    &mainViewParams,
    (useAA && currentAATechnique == AATechnique::TAA) ? getCurFrameTaaUvJitter()
                                                      : glm::vec2(0.f, 0.f),
    csmSplitLambda,
    csmShadowDist);

  if (!cfg.disableDirectionalLightsShadowsFeature)
  {
    auto& ls = sceneMgr->lightsRW();
    const auto [xNear, yNear, zNear, zFar] = mainViewParams.viewFrustum;

    const auto invView = glm::inverse(mainViewParams.mView);

    const float* splits = reinterpret_cast<const float*>(mainViewParams.csmFrustumSplits);

    for (int i = 0; i < CSM_CASCADE_COUNT; ++i)
    {
      const float zRangeMin = std::max(i == 0 ? zNear : splits[i - 1] - csmBlendingBeltSize, zNear);
      const float zRangeMax = std::min(splits[i] + csmBlendingBeltSize, zFar);
      const float xMin = xNear * zRangeMin / zNear;
      const float xMax = xNear * zRangeMax / zNear;
      const float yMin = yNear * zRangeMin / zNear;
      const float yMax = yNear * zRangeMax / zNear;

      const std::array subfrustumVerticesWorld{
        invView * glm::vec4{xMin, yMin, zRangeMin, 1.f},
        invView * glm::vec4{-xMin, yMin, zRangeMin, 1.f},
        invView * glm::vec4{-xMin, -yMin, zRangeMin, 1.f},
        invView * glm::vec4{xMin, -yMin, zRangeMin, 1.f},
        invView * glm::vec4{xMax, yMax, zRangeMax, 1.f},
        invView * glm::vec4{-xMax, yMax, zRangeMax, 1.f},
        invView * glm::vec4{-xMax, -yMax, zRangeMax, 1.f},
        invView * glm::vec4{xMax, -yMax, zRangeMax, 1.f}};

      const glm::vec4 centerWorld = [&] {
        auto res = glm::vec4{};
        for (const auto& v : subfrustumVerticesWorld)
          res += v;
        return res * 0.125f;
      }();
      const float radWorld = [&] {
        auto res = 0.f;
        for (const auto& v : subfrustumVerticesWorld)
          res = std::max(res, glm::length(v - centerWorld));
        return res;
      }();

      const float texelSize = (2.f * radWorld) / float(CSM_CASCADE_RESOLUTION);

      for (auto& dirl : std::span{ls.directionalLights, ls.directionalLightsCount})
      {
        auto& cascade = dirl.shadowmapCascades[i];

        // @TODO: pull stuff out
        OrthoCamera cam{};
        const auto dir = glm::normalize(dirl.direction);
        const auto up = std::max(fabsf(dir.x), fabsf(dir.z)) < SHADER_EPSILON
          ? glm::vec3(0.f, 0.f, 1.f)
          : glm::vec3(0.f, 1.f, 0.f);
        cam.lookAt({}, dir, up);
        const auto mLightView = cam.viewTm();

        auto centerView = mLightView * centerWorld;
        centerView.x = roundf(centerView.x / texelSize) * texelSize;
        centerView.y = roundf(centerView.y / texelSize) * texelSize;

        cascade.minX = centerView.x - radWorld;
        cascade.maxX = centerView.x + radWorld;
        cascade.minY = centerView.y - radWorld;
        cascade.maxY = centerView.y + radWorld;

        cascade.minZ = FLT_MAX;
        cascade.maxZ = -FLT_MAX;

        for (const auto& v : subfrustumVerticesWorld)
        {
          const auto viewV = mLightView * v;
          cascade.minZ = std::min(cascade.minZ, viewV.z);
          cascade.maxZ = std::max(cascade.maxZ, viewV.z);
        }
      }
    }
  }
}

void WorldRenderer::renderScene(vk::CommandBuffer cmd_buf, SceneRenderPassInfo&& srpi)
{
  const auto passHasFragmentStage = [](SceneRenderingPass p) {
    return p == SceneRenderingPass::COLOR || p == SceneRenderingPass::WIRE_COLOR ||
      p == SceneRenderingPass::COLOR_AFTER_PREPASS ||
      p == SceneRenderingPass::WIRE_COLOR_AFTER_PREPASS;
  };

  const bool needToDrawScene = drawScene && (srpi.flags & SRPO_STATIC);
  const bool needToDrawTerrain = terrain && drawTerrain && (srpi.flags & SRPO_TERRAIN);
  const bool needToDrawVegetation = vegetation && drawVegetation && (srpi.flags & SRPO_VEGETATION);

  if (!srpi.skipCulling)
  {
    srpi.vctx->update(srpi.vparams);
    viewCtxMgr->cullForView(cmd_buf, *srpi.vctx, srpi.vparams, constants->get());
  }

  if (srpi.vparams.needReverseZ)
    srpi.rtargetInfo.depthAttachment.clearDepthStencilValue = {0.f, 0};

  {
    ETNA_PROFILE_GPU(cmd_buf, renderScene);

    auto sceneDset = [&, this]() -> std::optional<etna::DescriptorSet> {
      if (needToDrawScene)
      {
        return etna::create_descriptor_set(
          staticMeshPipeline->getProg(srpi.pass).getDescriptorLayoutId(0),
          cmd_buf,
          {etna::Binding{0, sceneMgr->getInstanceMatricesBuf().genBinding()},
           etna::Binding{1, srpi.vctx->culledInstancesBuf.genBinding()},
           etna::Binding{8, constants->get().genBinding()},
           etna::Binding{9, srpi.vctx->viewParamsBuf.get().genBinding()},
           etna::Binding{10, srpi.vctx->viewDataBuf.genBinding()}});
      }
      else
      {
        return std::nullopt;
      }
    }();
    auto terrainDset = [&, this]() -> std::optional<etna::DescriptorSet> {
      if (needToDrawTerrain)
      {
        std::vector<etna::Binding> terrainBinds{};
        terrainBinds.reserve(
          terrain->geometryLevelsSamplerBindings.size() +
          terrain->normalLevelsSamplerBindings.size() +
          terrain->albedoLevelsSamplerBindings.size() +
          terrain->matdataLevelsSamplerBindings.size() + 4);
        terrainBinds.emplace_back(0, sceneMgr->getBboxesBuf().genBinding());
        terrainBinds.emplace_back(1, srpi.vctx->culledInstancesBuf.genBinding());
        for (const auto& b : terrain->geometryLevelsSamplerBindings)
          terrainBinds.push_back(b);
        for (const auto& b : terrain->normalLevelsSamplerBindings)
          terrainBinds.push_back(b);
        for (const auto& b : terrain->albedoLevelsSamplerBindings)
          terrainBinds.push_back(b);
        for (const auto& b : terrain->matdataLevelsSamplerBindings)
          terrainBinds.push_back(b);
        terrainBinds.emplace_back(7, terrain->source.genBinding());
        terrainBinds.emplace_back(8, constants->get().genBinding());
        terrainBinds.emplace_back(9, srpi.vctx->viewParamsBuf.get().genBinding());
        terrainBinds.emplace_back(10, srpi.vctx->viewDataBuf.genBinding());

        return etna::create_descriptor_set(
          terrainMeshPipeline->getProg(srpi.pass).getDescriptorLayoutId(0), cmd_buf, terrainBinds);
      }
      else
      {
        return std::nullopt;
      }
    }();
    auto vegetationDset = [&, this]() -> std::optional<etna::DescriptorSet> {
      if (needToDrawVegetation)
      {
        return etna::create_descriptor_set(
          vegetationMeshPipeline->getProg(srpi.pass).getDescriptorLayoutId(0),
          cmd_buf,
          {etna::Binding{0, vegetation->grassInstancesBuffer.genBinding()},
           etna::Binding{7, terrain->source.genBinding()},
           etna::Binding{8, constants->get().genBinding()},
           etna::Binding{9, srpi.vctx->viewParamsBuf.get().genBinding()},
           etna::Binding{10, srpi.vctx->viewDataBuf.genBinding()}});
      }
      else
      {
        return std::nullopt;
      }
    }();

    etna::RenderTargetState renderTargets{cmd_buf, srpi.rtargetInfo};

    cmd_buf.setDepthBiasEnable(vk::Bool32(srpi.depthBias));
    if (srpi.depthBias)
    {
      cmd_buf.setDepthBias(
        srpi.depthBiasConstantFactor, srpi.depthBiasClamp, srpi.depthBiasSlopeFactor);
    }

    if (needToDrawScene)
    {
      ETNA_PROFILE_GPU(cmd_buf, sceneMeshes);

      const auto& pipe = staticMeshPipeline->get(srpi.pass, bool(srpi.vparams.needReverseZ));
      std::vector vkSets{sceneDset->getVkSet()};

      if (passHasFragmentStage(srpi.pass))
      {
        vkSets.push_back(materialParamsDsetFrag.getVkSet());
        vkSets.push_back(bindlessTexturesDsetFrag.getVkSet());
        vkSets.push_back(bindlessSamplersDsetFrag.getVkSet());
      }

      cmd_buf.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, pipe.getVkPipelineLayout(), 0, vkSets, {});

      cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipe.getVkPipeline());

      cmd_buf.bindVertexBuffers(0, {sceneMgr->getVertexBuffer()}, {0});
      cmd_buf.bindIndexBuffer(sceneMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

      auto [offset, count] = sceneMgr->getSceneObjectsIndirectCommandsSubrange();

      cmd_buf.drawIndexedIndirect(
        srpi.vctx->indirectDrawBuf.get(), offset, count, sizeof(IndirectCommand));
    }

    if (needToDrawTerrain)
    {
      ETNA_PROFILE_GPU(cmd_buf, terrain);

      const auto& pipe = terrainMeshPipeline->get(srpi.pass, bool(srpi.vparams.needReverseZ));

      cmd_buf.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        pipe.getVkPipelineLayout(),
        0,
        {terrainDset->getVkSet()},
        {});

      cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipe.getVkPipeline());

      auto [offset, count] = sceneMgr->getTerrainIndirectCommandsSubrange();

      cmd_buf.pushConstants<shader_uint>(
        pipe.getVkPipelineLayout(),
        vk::ShaderStageFlagBits::eVertex,
        0,
        shader_uint(sceneMgr->getIndirectCommands()[offset].firstInstance));

      cmd_buf.drawIndexedIndirect(
        srpi.vctx->indirectDrawBuf.get(),
        offset * sizeof(IndirectCommand),
        count,
        sizeof(IndirectCommand));
    }

    if (needToDrawVegetation)
    {
      ETNA_PROFILE_GPU(cmd_buf, sceneVegetation);

      const auto& pipe = vegetationMeshPipeline->get(srpi.pass, bool(srpi.vparams.needReverseZ));
      std::vector vkSets{vegetationDset->getVkSet()};

      // We always have to sample opacity, even in depth pass
      vkSets.push_back(materialParamsDsetFrag.getVkSet());
      vkSets.push_back(bindlessTexturesDsetFrag.getVkSet());
      vkSets.push_back(bindlessSamplersDsetFrag.getVkSet());

      cmd_buf.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics, pipe.getVkPipelineLayout(), 0, vkSets, {});
      cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipe.getVkPipeline());

      cmd_buf.drawIndirect(
        sceneMgr->getVegetationIndirectDrawBuf().get(), 0, 1, sizeof(IndirectCommand));
    }
  }
}

void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  auto readyTids = sceneMgr->tickTransfer(cmd_buf);
  if (!readyTids.empty())
  {
    std::vector<etna::Binding> newBindings;
    for (TexId tid : readyTids)
    {
      const auto& tex = sceneMgr->getTex(tid);
      etna::Image::ViewParams vps{};
      if (tex.getCreationFlags() & vk::ImageCreateFlagBits::eCubeCompatible)
        vps.type = vk::ImageViewType::eCube;
      newBindings.emplace_back(etna::Binding{
        0, tex.genBinding({}, vk::ImageLayout::eShaderReadOnlyOptimal, vps), uint32_t(tid)});
      registerManagedImage(tex, fmt::format("bindless_tex_{}[{}]", size_t(tid), tex.getName()));
    }

    bindlessTexturesDsetFrag.updateBindings(newBindings);
    bindlessTexturesDsetComp.updateBindings(newBindings);

    if (!initialTransition)
    {
      bindlessTexturesDsetFrag.processBarriers(cmd_buf);
      bindlessTexturesDsetComp.processBarriers(cmd_buf);
    }

    if (std::any_of(readyTids.begin(), readyTids.end(), [this](TexId tid) {
          return sceneMgr->isTerrainTexture(tid);
        }))
    {
      queueClipmapInvalidation();
    }
  }

  // @NOTE: can be more adaptive
  if (!sceneMgr->canRender())
  {
    etna::RenderTargetState renderTargets(
      cmd_buf,
      {{0, 0}, {resolution.x, resolution.y}},
      {{.image = target_image, .view = target_image_view, .loadOp = vk::AttachmentLoadOp::eClear}},
      {});
    return;
  }

  memcpy(constants->get().data(), &constantsData, sizeof(constantsData));
  memcpy(lights->get().data(), &sceneMgr->getLights(), sizeof(sceneMgr->getLights()));

  // @TODO: unhack
  if (initialTransition)
  {
    materialParamsDsetFrag.processBarriers(cmd_buf);
    bindlessTexturesDsetFrag.processBarriers(cmd_buf);
    bindlessSamplersDsetFrag.processBarriers(cmd_buf);
    materialParamsDsetComp.processBarriers(cmd_buf);
    bindlessTexturesDsetComp.processBarriers(cmd_buf);
    bindlessSamplersDsetComp.processBarriers(cmd_buf);

    initialTransition = false;
  }

  {
    ETNA_PROFILE_GPU(cmd_buf, renderDeferred);

    if (terrain && terrain->needToroidalUpdate)
    {
      ETNA_PROFILE_GPU(cmd_buf, generateClipmap);

      terrain->needToroidalUpdate = false;

      {
        auto programInfo = etna::get_shader_program("clipmap_gen");
        std::vector<etna::Binding> bindings{};
        bindings.reserve(
          terrain->geometryLevelsBindings.size() + terrain->normalLevelsBindings.size() +
          terrain->albedoLevelsBindings.size() + terrain->matdataLevelsBindings.size() + 2);
        for (const auto& b : terrain->geometryLevelsBindings)
          bindings.push_back(b);
        for (const auto& b : terrain->normalLevelsBindings)
          bindings.push_back(b);
        for (const auto& b : terrain->albedoLevelsBindings)
          bindings.push_back(b);
        for (const auto& b : terrain->matdataLevelsBindings)
          bindings.push_back(b);
        bindings.emplace_back(7, terrain->source.genBinding());
        bindings.emplace_back(8, constants->get().genBinding());

        auto set =
          etna::create_descriptor_set(programInfo.getDescriptorLayoutId(0), cmd_buf, bindings);
        etna::flush_barriers(cmd_buf);

        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          generateClipmapPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet(),
           materialParamsDsetComp.getVkSet(),
           bindlessTexturesDsetComp.getVkSet(),
           bindlessSamplersDsetComp.getVkSet()},
          {});

        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute, generateClipmapPipeline.getVkPipeline());

        for (size_t i = 0; i < CLIPMAP_LEVEL_COUNT; ++i)
        {
          const auto dims = calculate_toroidal_dims(constantsData.toroidalOffset, shader_uint(i));
          ETNA_ASSERT(
            glm::abs(dims) % glm::ivec2(1 << (CLIPMAP_LEVEL_COUNT - 1 - i)) == glm::ivec2(0, 0));

          cmd_buf.pushConstants<shader_uint>(
            generateClipmapPipeline.getVkPipelineLayout(),
            vk::ShaderStageFlagBits::eCompute,
            0,
            shader_uint(i));
          cmd_buf.dispatch(
            get_linear_wg_count(
              calculate_thread_count_for_clipmap_update(dims), CLIPMAP_WORK_GROUP_SIZE),
            1,
            1);
        }
      }

      emit_barriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .srcAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
          .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
          .buffer = terrain->chunkHeightBoundsBuf.get(),
          .size = sizeof(ChunkHeightBoundsData)}});

      {
        auto programInfo = etna::get_shader_program("reset_terrain_chunk_height_bounds");
        auto set = etna::create_descriptor_set(
          programInfo.getDescriptorLayoutId(0),
          cmd_buf,
          {etna::Binding{0, terrain->chunkHeightBoundsBuf.genBinding()}});

        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          resetTerrainChunkHeightBoundsPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});

        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute, resetTerrainChunkHeightBoundsPipeline.getVkPipeline());
        cmd_buf.dispatch(
          get_linear_wg_count(TERRAIN_TOTAL_CHUNK_COUNT, BASE_WORK_GROUP_SIZE), 1, 1);
      }

      emit_barriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
          .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .dstAccessMask =
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
          .buffer = terrain->chunkHeightBoundsBuf.get(),
          .size = sizeof(ChunkHeightBoundsData)}});

      {
        auto programInfo = etna::get_shader_program("generate_terrain_chunk_height_bounds");
        std::vector<etna::Binding> bindings{};
        bindings.reserve(terrain->geometryLevelsBindings.size() + 2);
        for (const auto& b : terrain->geometryLevelsBindings)
          bindings.push_back(b);
        bindings.emplace_back(1, terrain->chunkHeightBoundsBuf.genBinding());
        bindings.emplace_back(8, constants->get().genBinding());

        auto set =
          etna::create_descriptor_set(programInfo.getDescriptorLayoutId(0), cmd_buf, bindings);
        etna::flush_barriers(cmd_buf);

        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          generateTerrainChunkHeightBoundsPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});

        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute,
          generateTerrainChunkHeightBoundsPipeline.getVkPipeline());

        for (size_t i = 0; i < CLIPMAP_LEVEL_COUNT; ++i)
        {
          cmd_buf.pushConstants<shader_uint>(
            generateClipmapPipeline.getVkPipelineLayout(),
            vk::ShaderStageFlagBits::eCompute,
            0,
            shader_uint(i));
          cmd_buf.dispatch(
            get_linear_wg_count(CLIPMAP_RESOLUTION, TERRAIN_CHUNK_HBOUNDS_WORK_GROUP_DIM),
            get_linear_wg_count(CLIPMAP_RESOLUTION, TERRAIN_CHUNK_HBOUNDS_WORK_GROUP_DIM),
            1);
        }
      }

      emit_barriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
           .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
           .srcAccessMask =
             vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
           .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
           .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
           .buffer = terrain->chunkHeightBoundsBuf.get(),
           .size = sizeof(ChunkHeightBoundsData)},
         vk::BufferMemoryBarrier2{
           .srcStageMask =
             vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eComputeShader,
           .srcAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
           .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
           .dstAccessMask =
             vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
           .buffer = sceneMgr->getBboxesBuf().get(),
           .size = sceneMgr->getBboxes().size_bytes()}});

      {
        auto programInfo = etna::get_shader_program("transfer_terrain_chunk_height_bounds");
        auto set = etna::create_descriptor_set(
          programInfo.getDescriptorLayoutId(0),
          cmd_buf,
          {etna::Binding{0, sceneMgr->getBboxesBuf().genBinding()},
           etna::Binding{1, terrain->chunkHeightBoundsBuf.genBinding()},
           etna::Binding{8, constants->get().genBinding()}});

        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          transferTerrainChunkHeightBoundsPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});

        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute,
          transferTerrainChunkHeightBoundsPipeline.getVkPipeline());
        cmd_buf.dispatch(
          get_linear_wg_count(TERRAIN_TOTAL_CHUNK_COUNT, BASE_WORK_GROUP_SIZE), 1, 1);
      }

      emit_barriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .srcAccessMask =
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
          .dstStageMask =
            vk::PipelineStageFlagBits2::eVertexShader | vk::PipelineStageFlagBits2::eComputeShader,
          .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
          .buffer = sceneMgr->getBboxesBuf().get(),
          .size = sceneMgr->getBboxes().size_bytes()}});
    }

    if (water)
    {
      ETNA_PROFILE_GPU(cmd_buf, waterGen);

      if (waterSettingsDirty)
      {
        ETNA_PROFILE_GPU(cmd_buf, waterIndepGen);

        {
          ETNA_PROFILE_GPU(cmd_buf, waterIndepGenCalc);
          auto programInfo = etna::get_shader_program("water_time_indep_spectra_gen");
          std::vector<etna::Binding> binds{}; // @SPEED piggy
          for (int c = 0; c < WATER_CASCADE_COUNT; ++c)
          {
            binds.emplace_back(
              0,
              water->cascades[c].wavevectorFrequencyTex.genBinding({}, vk::ImageLayout::eGeneral),
              uint32_t(c));
            binds.emplace_back(
              1,
              water->cascades[c].timeIndepSpectraTex.genBinding({}, vk::ImageLayout::eGeneral),
              uint32_t(c));
          }
          binds.emplace_back(6, water->source.genBinding());
          binds.emplace_back(8, constants->get().genBinding());
          auto set = etna::create_descriptor_set(
            programInfo.getDescriptorLayoutId(0), cmd_buf, std::move(binds));
          etna::flush_barriers(cmd_buf);
          cmd_buf.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            waterInitalSpectraGenerate.getVkPipelineLayout(),
            0,
            {set.getVkSet(),
             materialParamsDsetComp.getVkSet(),
             bindlessTexturesDsetComp.getVkSet(),
             bindlessSamplersDsetComp.getVkSet()},
            {});
          cmd_buf.bindPipeline(
            vk::PipelineBindPoint::eCompute, waterInitalSpectraGenerate.getVkPipeline());
          cmd_buf.dispatch(
            get_linear_wg_count(WATER_CASCADE_RES, WATER_WORKGROUP_DIM),
            get_linear_wg_count(WATER_CASCADE_RES, WATER_WORKGROUP_DIM),
            WATER_CASCADE_COUNT);
        }

        {
          ETNA_PROFILE_GPU(cmd_buf, waterIndepGenCalc);
          auto programInfo = etna::get_shader_program("water_time_indep_spectra_conjugate");
          std::vector<etna::Binding> binds{}; // @SPEED piggy
          for (int c = 0; c < WATER_CASCADE_COUNT; ++c)
          {
            binds.emplace_back(
              0,
              water->cascades[c].timeIndepSpectraTex.genBinding({}, vk::ImageLayout::eGeneral),
              uint32_t(c));
          }
          auto set = etna::create_descriptor_set(
            programInfo.getDescriptorLayoutId(0), cmd_buf, std::move(binds));
          etna::flush_barriers(cmd_buf);
          cmd_buf.bindDescriptorSets(
            vk::PipelineBindPoint::eCompute,
            waterInitalSpectraConjugate.getVkPipelineLayout(),
            0,
            {set.getVkSet()},
            {});
          cmd_buf.bindPipeline(
            vk::PipelineBindPoint::eCompute, waterInitalSpectraConjugate.getVkPipeline());
          cmd_buf.dispatch(
            get_linear_wg_count(WATER_CASCADE_RES, WATER_WORKGROUP_DIM),
            get_linear_wg_count(WATER_CASCADE_RES, WATER_WORKGROUP_DIM),
            WATER_CASCADE_COUNT);
        }

        waterSettingsDirty = false;
      }

      {
        ETNA_PROFILE_GPU(cmd_buf, waterTimeDepGen);
        auto programInfo = etna::get_shader_program("water_time_spectra_gen");
        std::vector<etna::Binding> binds{}; // @SPEED piggy
        for (int c = 0; c < WATER_CASCADE_COUNT; ++c)
        {
          binds.emplace_back(
            0,
            water->cascades[c].spatialDisplacementAndDxzTex.genBinding(
              {}, vk::ImageLayout::eGeneral),
            uint32_t(c));
          binds.emplace_back(
            1,
            water->cascades[c].spatialDisplacementOtherDerivativesTex.genBinding(
              {}, vk::ImageLayout::eGeneral),
            uint32_t(c));
          binds.emplace_back(
            2,
            water->cascades[c].wavevectorFrequencyTex.genBinding({}, vk::ImageLayout::eGeneral),
            uint32_t(c));
          binds.emplace_back(
            3,
            water->cascades[c].timeIndepSpectraTex.genBinding({}, vk::ImageLayout::eGeneral),
            uint32_t(c));
        }
        binds.emplace_back(8, constants->get().genBinding());
        auto set = etna::create_descriptor_set(
          programInfo.getDescriptorLayoutId(0), cmd_buf, std::move(binds));
        etna::flush_barriers(cmd_buf);
        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          waterTimeSpectraGenerate.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});
        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute, waterTimeSpectraGenerate.getVkPipeline());
        cmd_buf.dispatch(
          get_linear_wg_count(WATER_CASCADE_RES, WATER_WORKGROUP_DIM),
          get_linear_wg_count(WATER_CASCADE_RES, WATER_WORKGROUP_DIM),
          WATER_CASCADE_COUNT);
      }
    }

    if (drawVegetation)
    {
      ETNA_PROFILE_GPU(cmd_buf, grassGen);

      mainViewContext->update(mainViewParams);

      emit_barriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .srcAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
          .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
          .buffer = vegetation->culledChunkBuffer.get(),
          .size = vegChunkBufferSizeBytes()}});

      {
        auto programInfo = etna::get_shader_program("grass_generate_clear_chunks");
        auto set = etna::create_descriptor_set(
          programInfo.getDescriptorLayoutId(0),
          cmd_buf,
          {etna::Binding{0, vegetation->culledChunkBuffer.genBinding()}});
        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          vegetationGenerateClearChunks.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});
        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute, vegetationGenerateClearChunks.getVkPipeline());
        cmd_buf.dispatch(1, 1, 1);
      }

      emit_barriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
          .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .dstAccessMask =
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
          .buffer = vegetation->culledChunkBuffer.get(),
          .size = vegChunkBufferSizeBytes()}});

      {
        auto programInfo = etna::get_shader_program("grass_generate_cull_chunks");
        auto set = etna::create_descriptor_set(
          programInfo.getDescriptorLayoutId(0),
          cmd_buf,
          {
            etna::Binding{8, constants->get().genBinding()},
            etna::Binding{9, mainViewContext->viewParamsBuf.get().genBinding()},
            etna::Binding{10, vegetation->culledChunkBuffer.genBinding()},
          });
        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          vegetationGenerateCullChunks.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});
        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute, vegetationGenerateCullChunks.getVkPipeline());
        cmd_buf.dispatch(
          get_linear_wg_count(VEGETATION_GRID_EXTENT, VEGETATION_CHUNK_CULL_GROUP_DIM),
          get_linear_wg_count(VEGETATION_GRID_EXTENT, VEGETATION_CHUNK_CULL_GROUP_DIM),
          1);
      }

      if (sortVegChunks)
      {
        emit_barriers(
          cmd_buf,
          {vk::BufferMemoryBarrier2{
            .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .srcAccessMask =
              vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
            .dstAccessMask =
              vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
            .buffer = vegetation->culledChunkBuffer.get(),
            .size = vegChunkBufferSizeBytes()}});
        auto programInfo = etna::get_shader_program("grass_generate_sort_chunks");
        auto set = etna::create_descriptor_set(
          programInfo.getDescriptorLayoutId(0),
          cmd_buf,
          {
            etna::Binding{0, vegetation->culledChunkBuffer.genBinding()},
            etna::Binding{9, mainViewContext->viewParamsBuf.get().genBinding()},
          });
        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          vegetationGenerateSortChunks.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});
        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute, vegetationGenerateSortChunks.getVkPipeline());
        cmd_buf.dispatch(1, 1, 1);
      }

      emit_barriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .srcAccessMask =
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
          .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
          .buffer = vegetation->culledChunkBuffer.get(),
          .size = vegChunkBufferSizeBytes()}});
      emit_barriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
          .srcAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
          .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
          .buffer = vegetation->indirectDispatchBuffer.get(),
          .size = terrain->sourceData.detailCount * sizeof(IndirectDispatchCommand)}});

      {
        auto programInfo = etna::get_shader_program("grass_generate_prepare_inst_command");
        auto set = etna::create_descriptor_set(
          programInfo.getDescriptorLayoutId(0),
          cmd_buf,
          {etna::Binding{0, vegetation->culledChunkBuffer.genBinding()},
           etna::Binding{1, vegetation->indirectDispatchBuffer.genBinding()},
           etna::Binding{7, terrain->source.genBinding()}});
        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          vegetationGeneratePrepareInstCommand.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});
        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute, vegetationGeneratePrepareInstCommand.getVkPipeline());
        cmd_buf.dispatch(
          get_linear_wg_count(terrain->sourceData.detailCount, BASE_WORK_GROUP_SIZE), 1, 1);
      }

      emit_barriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
          .srcAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
          .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
          .buffer = sceneMgr->getVegetationIndirectDrawBuf().get(),
          .size = sizeof(IndirectCommand)}});

      viewCtxMgr->resetIndirectBufNoBarriers(cmd_buf, sceneMgr->getVegetationIndirectDrawBuf(), 1);

      emit_barriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
          .dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
          .dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
          .buffer = vegetation->indirectDispatchBuffer.get(),
          .size = terrain->sourceData.detailCount * sizeof(IndirectDispatchCommand)}});
      emit_barriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
          .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .dstAccessMask =
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
          .buffer = sceneMgr->getVegetationIndirectDrawBuf().get(),
          .size = sizeof(IndirectCommand)}});
      emit_barriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eVertexShader,
          .srcAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
          .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
          .buffer = vegetation->grassInstancesBuffer.get(),
          .size = vegInstBufferSizeBytes()}});

      {
        auto programInfo = etna::get_shader_program("grass_generate_instances");
        std::vector<etna::Binding> binds{};
        binds.reserve(
          terrain->geometryLevelsSamplerBindings.size() +
          terrain->normalLevelsSamplerBindings.size() +
          terrain->albedoLevelsSamplerBindings.size() +
          terrain->matdataLevelsSamplerBindings.size() + 7);
        for (const auto& b : terrain->geometryLevelsSamplerBindings)
          binds.push_back(b);
        for (const auto& b : terrain->normalLevelsSamplerBindings)
          binds.push_back(b);
        for (const auto& b : terrain->albedoLevelsSamplerBindings)
          binds.push_back(b);
        for (const auto& b : terrain->matdataLevelsSamplerBindings)
          binds.push_back(b);
        binds.emplace_back(7, terrain->source.genBinding());
        binds.emplace_back(8, constants->get().genBinding());
        binds.emplace_back(9, mainViewContext->viewParamsBuf.get().genBinding());
        binds.emplace_back(10, vegetation->culledChunkBuffer.genBinding());
        binds.emplace_back(11, sceneMgr->getVegetationTemplateBuf().genBinding());
        binds.emplace_back(12, sceneMgr->getVegetationIndirectDrawBuf().genBinding());
        binds.emplace_back(13, vegetation->grassInstancesBuffer.genBinding());
        auto set =
          etna::create_descriptor_set(programInfo.getDescriptorLayoutId(0), cmd_buf, binds);
        etna::flush_barriers(cmd_buf);
        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          vegetationGenerateInstances.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});
        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute, vegetationGenerateInstances.getVkPipeline());

        for (size_t detId = 0; detId < terrain->sourceData.detailCount; ++detId)
        {
          if (terrain->sourceData.details[detId].vegetationId == shader_uint(-1))
            continue;
          if (detId > 0)
          {
            emit_barriers(
              cmd_buf,
              {vk::BufferMemoryBarrier2{
                .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .srcAccessMask = vk::AccessFlagBits2::eShaderStorageRead |
                  vk::AccessFlagBits2::eShaderStorageWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead |
                  vk::AccessFlagBits2::eShaderStorageWrite,
                .buffer = sceneMgr->getVegetationIndirectDrawBuf().get(),
                .size = sizeof(IndirectCommand)}});
            emit_barriers(
              cmd_buf,
              {vk::BufferMemoryBarrier2{
                .srcStageMask = vk::PipelineStageFlagBits2::eVertexShader,
                .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
                .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
                .buffer = vegetation->grassInstancesBuffer.get(),
                .size = vegInstBufferSizeBytes()}});
          }
          cmd_buf.pushConstants<shader_uint>(
            vegetationGenerateInstances.getVkPipelineLayout(),
            vk::ShaderStageFlagBits::eCompute,
            0,
            shader_uint(detId));
          cmd_buf.dispatchIndirect(
            vegetation->indirectDispatchBuffer.get(), detId * sizeof(IndirectDispatchCommand));
        }
      }

      emit_barriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .srcAccessMask =
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
          .dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
          .dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
          .buffer = sceneMgr->getVegetationIndirectDrawBuf().get(),
          .size = sizeof(IndirectCommand)}});
      emit_barriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
          .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader,
          .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
          .buffer = vegetation->grassInstancesBuffer.get(),
          .size = vegInstBufferSizeBytes()}});
    }

    emit_barriers(
      cmd_buf,
      {vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .buffer = lightMatricesBuf.get(),
        .size = sizeof(LightMatrices)}});

    {
      ETNA_PROFILE_GPU(cmd_buf, shadowmapGen);

      DEFER([&cmd_buf] { cmd_buf.setDepthBiasEnable(VK_FALSE); });

      const auto& ls = sceneMgr->getLights();

      auto transferMat = [this, &cmd_buf](auto lid, const ViewContext& ctx) {
        auto programInfo = etna::get_shader_program("transfer_light_mats");
        auto set = etna::create_descriptor_set(
          programInfo.getDescriptorLayoutId(0),
          cmd_buf,
          {etna::Binding{0, lightMatricesBuf.genBinding()},
           etna::Binding{9, ctx.viewParamsBuf.get().genBinding()},
           etna::Binding{10, ctx.viewDataBuf.genBinding()}});

        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          transferLightMatsPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});

        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute, transferLightMatsPipeline.getVkPipeline());
        cmd_buf.pushConstants<shader_uint>(
          transferLightMatsPipeline.getVkPipelineLayout(),
          vk::ShaderStageFlagBits::eCompute,
          0,
          shader_uint(lid));

        cmd_buf.dispatch(1, 1, 1);
      };

      // @TODO: cull lights outside of frustum. Maybe also draw sm-s on demand?

      constexpr size_t MAX_STATIC_LIGHTS_PER_FRAME = 32;
      size_t processedStaticLights = 0;

      if (pointLightShadowsSettings.enable)
      {
        for (size_t i = 0; const auto& point : std::span{ls.pointLights, ls.pointLightsCount})
        {
          DEFER([&i] { ++i; });

          if (
            !pointLightsSettingsDirty &&
            shader_veq(point.position, prevLights.pointLights[i].position) &&
            shader_feq(point.range, prevLights.pointLights[i].range))
          {
            continue;
          }

          if (processedStaticLights >= MAX_STATIC_LIGHTS_PER_FRAME)
            break;
          ++processedStaticLights;

          prevLights.pointLights[i] = point;

          const auto [tid, _] = unpack_tex_smp_id_pair(point.shadowmap);
          const auto& map = sceneMgr->getTex(tid);

          constexpr std::array FACE_DIRS{
            glm::vec3{1.f, 0.f, 0.f},
            glm::vec3{-1.f, 0.f, 0.f},
            glm::vec3{0.f, 1.f, 0.f},
            glm::vec3{0.f, -1.f, 0.f},
            glm::vec3{0.f, 0.f, -1.f},
            glm::vec3{0.f, 0.f, 1.f},
          };
          constexpr std::array FACE_UPS{
            glm::vec3{0.f, 1.f, 0.f},
            glm::vec3{0.f, 1.f, 0.f},
            glm::vec3{0.f, 0.f, 1.f},
            glm::vec3{0.f, 0.f, -1.f},
            glm::vec3{0.f, 1.f, 0.f},
            glm::vec3{0.f, 1.f, 0.f},
          };

          for (size_t j = 0; j < 6; ++j)
          {
            Camera cam{};
            cam.lookAt(point.position, point.position + FACE_DIRS[j], FACE_UPS[j]);
            cam.fov = 90.f;
            cam.zNear = 0.001f;
            cam.zFar = point.range + 0.001f;

            renderScene(
              cmd_buf,
              {.pass = pointLightShadowsSettings.frontFaceCull
                 ? SceneRenderingPass::SHADOW_FRONT_CULLED
                 : SceneRenderingPass::SHADOW,
               .flags = SRPO_STATIC | SRPO_TERRAIN,
               .vctx = &pointLightViews[i][j],
               .vparams =
                 view_params_for_cam(cam, 1.f, true, false, nullptr), // @TODO: reverse depth is
                                                                      // broken on point lights
               .rtargetInfo =
                 {{{0, 0}, {POINT_SM_RESOLUTION, POINT_SM_RESOLUTION}},
                  {},
                  {.image = map.get(),
                   .view = map.getView({.baseLayer = uint32_t(j), .layerCount = 1u})}},
               .depthBias = pointLightShadowsSettings.depthBias,
               .depthBiasConstantFactor = pointLightShadowsSettings.depthBiasConstantFactor,
               .depthBiasClamp = pointLightShadowsSettings.depthBiasClamp,
               .depthBiasSlopeFactor = pointLightShadowsSettings.depthBiasSlopeFactor});

            transferMat(j + i * 6, pointLightViews[i][j]);
          }

          etna::set_state(
            cmd_buf,
            map.get(),
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::AccessFlagBits2::eShaderRead,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eDepth);
        }

        pointLightsSettingsDirty = false;
      }

      if (spotLightShadowsSettings.enable)
      {
        cmd_buf.setDepthBiasEnable(spotLightShadowsSettings.depthBias ? VK_TRUE : VK_FALSE);
        cmd_buf.setDepthBias(
          spotLightShadowsSettings.depthBiasConstantFactor,
          spotLightShadowsSettings.depthBiasClamp,
          spotLightShadowsSettings.depthBiasSlopeFactor);

        for (size_t i = 0; const auto& spot : std::span{ls.spotLights, ls.spotLightsCount})
        {
          DEFER([&i] { ++i; });

          if (
            !spotLightsSettingsDirty &&
            shader_veq(spot.position, prevLights.spotLights[i].position) &&
            shader_veq(spot.direction, prevLights.spotLights[i].direction) &&
            shader_feq(spot.range, prevLights.spotLights[i].range) &&
            shader_feq(spot.outerConeAngle, prevLights.spotLights[i].outerConeAngle))
          {
            continue;
          }

          if (processedStaticLights >= MAX_STATIC_LIGHTS_PER_FRAME)
            break;
          ++processedStaticLights;

          prevLights.spotLights[i] = spot;

          const auto [tid, _] = unpack_tex_smp_id_pair(spot.shadowmap);
          const auto& map = sceneMgr->getTex(tid);

          // @TODO: pull stuff out
          Camera cam{};
          const auto dir = glm::normalize(spot.direction);
          const auto up = std::max(fabsf(dir.x), fabsf(dir.z)) < SHADER_EPSILON
            ? glm::vec3(0.f, 0.f, 1.f)
            : glm::vec3(0.f, 1.f, 0.f);
          cam.lookAt(spot.position, spot.position + dir, up);
          cam.fov = spot.outerConeAngle * 180.f / float(M_PI);
          cam.zNear = 0.001f;
          cam.zFar = spot.range + 0.001f;

          renderScene(
            cmd_buf,
            {.pass = spotLightShadowsSettings.frontFaceCull
               ? SceneRenderingPass::SHADOW_FRONT_CULLED
               : SceneRenderingPass::SHADOW,
             .flags = SRPO_STATIC | SRPO_TERRAIN,
             .vctx = &spotLightViews[i],
             .vparams = view_params_for_cam(
               cam, 1.f, true, false, nullptr), // @TODO: reverse depth is broken on spot lights
             .rtargetInfo =
               {{{0, 0}, {SPOT_SM_RESOLUTION, SPOT_SM_RESOLUTION}},
                {},
                {.image = map.get(), .view = map.getView({})}},
             .depthBias = spotLightShadowsSettings.depthBias,
             .depthBiasConstantFactor = spotLightShadowsSettings.depthBiasConstantFactor,
             .depthBiasClamp = spotLightShadowsSettings.depthBiasClamp,
             .depthBiasSlopeFactor = spotLightShadowsSettings.depthBiasSlopeFactor});

          etna::set_state(
            cmd_buf,
            map.get(),
            vk::PipelineStageFlagBits2::eFragmentShader,
            vk::AccessFlagBits2::eShaderRead,
            vk::ImageLayout::eShaderReadOnlyOptimal,
            vk::ImageAspectFlagBits::eDepth);

          transferMat(i + POINT_LIGHT_BUF_SIZE * 6, spotLightViews[i]);
        }

        spotLightsSettingsDirty = false;
      }

      if (directionalLightShadowsSettings.enable)
      {
        cmd_buf.setDepthBiasEnable(directionalLightShadowsSettings.depthBias ? VK_TRUE : VK_FALSE);
        cmd_buf.setDepthBias(
          directionalLightShadowsSettings.depthBiasConstantFactor,
          directionalLightShadowsSettings.depthBiasClamp,
          directionalLightShadowsSettings.depthBiasSlopeFactor);

        for (size_t i = 0;
             const auto& dirl : std::span{ls.directionalLights, ls.directionalLightsCount})
        {
          // @NOTE: not bothering skipping cascades cuz camera turns
          DEFER([&i] { ++i; });

          for (size_t j = 0; j < CSM_CASCADE_COUNT; ++j)
          {
            const auto [tid, _] = unpack_tex_smp_id_pair(dirl.shadowmapCascades[j].map);
            const auto& map = sceneMgr->getTex(tid);

            const auto minX = dirl.shadowmapCascades[j].minX;
            const auto maxX = dirl.shadowmapCascades[j].maxX;
            const auto minY = dirl.shadowmapCascades[j].minY;
            const auto maxY = dirl.shadowmapCascades[j].maxY;
            const auto maxZ = dirl.shadowmapCascades[j].maxZ;

            // @TODO: pull stuff out
            OrthoCamera cam{};

            // Refined via depth bounds from culling on the gpu
            cam.zNear = -10000.f;
            cam.zFar = maxZ + 0.001f;

            const auto xExt = (maxX - minX) * 0.5f;
            const auto yExt = (maxY - minY) * 0.5f;
            const auto xCenter = (maxX + minX) * 0.5f;
            const auto yCenter = (maxY + minY) * 0.5f;

            const auto dir = glm::normalize(dirl.direction);
            const auto up = std::max(fabsf(dir.x), fabsf(dir.z)) < SHADER_EPSILON
              ? glm::vec3(0.f, 0.f, 1.f)
              : glm::vec3(0.f, 1.f, 0.f);
            // @TODO: fix this properly in spot lights too
            const auto xdir = glm::normalize(glm::cross(up, dir));
            const auto ydir = glm::normalize(glm::cross(dir, xdir));

            const auto position = xdir * xCenter + ydir * yCenter;
            cam.lookAt(position, position + dir, up);

            renderScene(
              cmd_buf,
              {.pass = directionalLightShadowsSettings.frontFaceCull
                 ? SceneRenderingPass::SHADOW_FRONT_CULLED
                 : SceneRenderingPass::SHADOW,
               .flags = SRPO_STATIC | SRPO_TERRAIN,
               .vctx = &directionalLightCascadeViews[i][j],
               .vparams = view_params_for_cam(cam, xExt, yExt, true, nullptr),
               .rtargetInfo =
                 {{{0, 0}, {CSM_CASCADE_RESOLUTION, CSM_CASCADE_RESOLUTION}},
                  {},
                  {.image = map.get(), .view = map.getView({})}},
               .depthBias = directionalLightShadowsSettings.depthBias,
               .depthBiasConstantFactor = directionalLightShadowsSettings.depthBiasConstantFactor,
               .depthBiasClamp = directionalLightShadowsSettings.depthBiasClamp,
               .depthBiasSlopeFactor = directionalLightShadowsSettings.depthBiasSlopeFactor});

            etna::set_state(
              cmd_buf,
              map.get(),
              vk::PipelineStageFlagBits2::eFragmentShader,
              vk::AccessFlagBits2::eShaderRead,
              vk::ImageLayout::eShaderReadOnlyOptimal,
              vk::ImageAspectFlagBits::eDepth);

            transferMat(
              j + i * CSM_CASCADE_COUNT + POINT_LIGHT_BUF_SIZE * 6 + SPOT_LIGHT_BUF_SIZE,
              directionalLightCascadeViews[i][j]);
          }
        }

        directionalLightsSettingsDirty = false;
      }
    }

    emit_barriers(
      cmd_buf,
      {vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
        .buffer = lightMatricesBuf.get(),
        .size = sizeof(LightMatrices)}});

    constexpr auto Z_PREPASS_OBJ_MASK = SRPO_ALL;
    static_assert(Z_PREPASS_OBJ_MASK != 0);

    if (zPrepass)
    {
      ETNA_PROFILE_GPU(cmd_buf, zPrepass);

      renderScene(
        cmd_buf,
        {.pass =
           wireframe ? SceneRenderingPass::WIRE_DEPTH_PREPASS : SceneRenderingPass::DEPTH_PREPASS,
         .flags = Z_PREPASS_OBJ_MASK,
         .vctx = &mainViewContext.value(),
         .vparams = mainViewParams,
         .rtargetInfo = {
           {{0, 0}, {resolution.x, resolution.y}},
           {},
           {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})}}});
    }

    {
      ETNA_PROFILE_GPU(cmd_buf, deferredGpass);

      renderScene(
        cmd_buf,
        {.pass = wireframe
           ? (zPrepass ? SceneRenderingPass::WIRE_COLOR_AFTER_PREPASS
                       : SceneRenderingPass::WIRE_COLOR)
           : (zPrepass ? SceneRenderingPass::COLOR_AFTER_PREPASS : SceneRenderingPass::COLOR),
         .flags = zPrepass ? Z_PREPASS_OBJ_MASK : SRPO_ALL,
         .vctx = &mainViewContext.value(),
         .vparams = mainViewParams,
         .rtargetInfo =
           {{{0, 0}, {resolution.x, resolution.y}},
            {
              {.image = gbufAlbedo.get(), .view = gbufAlbedo.getView({})},
              {.image = gbufMaterial.get(), .view = gbufMaterial.getView({})},
              {.image = gbufNormal.get(), .view = gbufNormal.getView({})},
              {.image = gbufTransmission.get(), .view = gbufTransmission.getView({})},
              {.image = motionVectors.curBuf().get(), .view = motionVectors.curBuf().getView({})},
            },
            {.image = mainViewDepth.get(),
             .view = mainViewDepth.getView({}),
             .loadOp = zPrepass ? vk::AttachmentLoadOp::eLoad : vk::AttachmentLoadOp::eClear}},
         .skipCulling = zPrepass});
    }

    if (zPrepass && (Z_PREPASS_OBJ_MASK & SRPO_ALL) != SRPO_ALL)
    {
      ETNA_PROFILE_GPU(cmd_buf, deferredGpassNoZ);

      renderScene(
        cmd_buf,
        {.pass = wireframe ? SceneRenderingPass::WIRE_COLOR : SceneRenderingPass::COLOR,
         .flags = ~Z_PREPASS_OBJ_MASK,
         .vctx = &mainViewContext.value(),
         .vparams = mainViewParams,
         .rtargetInfo =
           {{{0, 0}, {resolution.x, resolution.y}},
            {{.image = gbufAlbedo.get(),
              .view = gbufAlbedo.getView({}),
              .loadOp = vk::AttachmentLoadOp::eLoad},
             {.image = gbufMaterial.get(),
              .view = gbufMaterial.getView({}),
              .loadOp = vk::AttachmentLoadOp::eLoad},
             {.image = gbufNormal.get(),
              .view = gbufNormal.getView({}),
              .loadOp = vk::AttachmentLoadOp::eLoad},
             {.image = gbufTransmission.get(),
              .view = gbufTransmission.getView({}),
              .loadOp = vk::AttachmentLoadOp::eLoad},
             {.image = motionVectors.curBuf().get(),
              .view = motionVectors.curBuf().getView({}),
              .loadOp = vk::AttachmentLoadOp::eLoad}},
            {.image = mainViewDepth.get(),
             .view = mainViewDepth.getView({}),
             .loadOp = vk::AttachmentLoadOp::eLoad}},
         .skipCulling = true});
    }

    if (useSsao)
    {
      ETNA_PROFILE_GPU(cmd_buf, ssao);

      {
        ETNA_PROFILE_GPU(cmd_buf, ssaoGen);
        auto set = etna::create_descriptor_set(
          ssaoGen->shaderProgramInfo().getDescriptorLayoutId(0),
          cmd_buf,
          {etna::Binding{
             0,
             gbufNormal.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
           etna::Binding{
             1,
             mainViewDepth.genBinding(
               defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
           etna::Binding{
             2,
             ssaoBuffer.prevBuf().genBinding(
               defaultMirrorSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
           etna::Binding{
             3,
             motionVectors.curBuf().genBinding(
               defaultMirrorSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
           etna::Binding{8, constants->get().genBinding()},
           etna::Binding{9, mainViewContext->viewParamsBuf.get().genBinding()},
           etna::Binding{10, mainViewContext->viewDataBuf.genBinding()}});

        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics, ssaoGen->pipelineLayout(), 0, {set.getVkSet()}, {});

        ssaoGen->render(cmd_buf, ssaoBuffer.curBuf().get(), ssaoBuffer.curBuf().getView({}));
      }

      {
        ETNA_PROFILE_GPU(cmd_buf, ssaoBlur);
        auto set = etna::create_descriptor_set(
          ssaoBlur->shaderProgramInfo().getDescriptorLayoutId(0),
          cmd_buf,
          {etna::Binding{
             0,
             ssaoBuffer.curBuf().genBinding(
               defaultMirrorSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
           etna::Binding{8, constants->get().genBinding()},
           etna::Binding{9, mainViewContext->viewParamsBuf.get().genBinding()},
           etna::Binding{10, mainViewContext->viewDataBuf.genBinding()}});

        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics, ssaoBlur->pipelineLayout(), 0, {set.getVkSet()}, {});

        ssaoBlur->render(cmd_buf, ssaoBlurredBuffer.get(), ssaoBlurredBuffer.getView({}));
      }
    }

    {
      ETNA_PROFILE_GPU(cmd_buf, deferredResolve);

      auto set = etna::create_descriptor_set(
        gbufferResolver->shaderProgramInfo().getDescriptorLayoutId(0),
        cmd_buf,
        {etna::Binding{1, lights->get().genBinding()},
         etna::Binding{2, lightMatricesBuf.genBinding()},
         etna::Binding{
           3, gbufAlbedo.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
         etna::Binding{
           4,
           gbufMaterial.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
         etna::Binding{
           5, gbufNormal.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
         etna::Binding{
           6,
           gbufTransmission.genBinding(
             defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
         etna::Binding{
           7,
           mainViewDepth.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
         etna::Binding{8, constants->get().genBinding()},
         etna::Binding{9, mainViewContext->viewParamsBuf.get().genBinding()},
         etna::Binding{10, mainViewContext->viewDataBuf.genBinding()},
         etna::Binding{11, (skybox ? skybox->source : stubUniBuffer).genBinding()},
         etna::Binding{
           12,
           ssaoBlurredBuffer.genBinding(
             defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}});

      cmd_buf.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        gbufferResolver->pipelineLayout(),
        0,
        {set.getVkSet(),
         materialParamsDsetFrag.getVkSet(),
         bindlessTexturesDsetFrag.getVkSet(),
         bindlessSamplersDsetFrag.getVkSet()},
        {});

      gbufferResolver->render(cmd_buf, hdrTarget.get(), hdrTarget.getView({}));
    }

    {
      ETNA_PROFILE_GPU(cmd_buf, tonemapping);

      const auto& target = useAA ? ldrTarget.get() : target_image;
      const auto& targetView = useAA ? ldrTarget.getView({}) : target_image_view;

      tonemapperComps[size_t(currentTonemappingTechnique)]->tonemap(
        cmd_buf, target, targetView, hdrTarget, defaultSampler, constants->get());
    }

    if (useAA)
    {
      ETNA_PROFILE_GPU(cmd_buf, antialiasing);

      aaComps[size_t(currentAATechnique)]->antialias(
        cmd_buf, target_image, target_image_view, ldrTarget, defaultSampler, constants->get());
    }

    {
      ETNA_PROFILE_GPU(cmd_buf, debugDrawing);

      if (drawBboxes)
      {
        bboxRenderer->render(
          cmd_buf,
          {{{0, 0}, {resolution.x, resolution.y}},
           {{.image = target_image,
             .view = target_image_view,
             .loadOp = vk::AttachmentLoadOp::eLoad}},
           {.image = mainViewDepth.get(),
            .view = mainViewDepth.getView({}),
            .loadOp = vk::AttachmentLoadOp::eLoad}},
          sceneMgr->getInstanceMatricesBuf(),
          sceneMgr->getInstancesBuf(),
          sceneMgr->getBboxesBuf(),
          constants->get(),
          mainViewContext->viewParamsBuf.get(),
          uint32_t(sceneMgr->getInstances().size()));
      }

      if (currentDebugDrawer)
        debugDrawers[*currentDebugDrawer].draw(cmd_buf, target_image, target_image_view);
    }
  }
}

static constexpr auto build_light_names_from_struct()
{
  std::vector<std::vector<std::string>> lightOptionsNames{};
  std::vector<std::string> none{"none"};
  std::vector<std::string> point{};
  std::vector<std::string> spot{};
  std::vector<std::string> dir{};
  for (shader_uint i = 0; i < POINT_LIGHT_BUF_SIZE; ++i)
    point.emplace_back(std::string{"point_"} + std::to_string(i));
  for (shader_uint i = 0; i < SPOT_LIGHT_BUF_SIZE; ++i)
    spot.emplace_back(std::string{"spot_"} + std::to_string(i));
  for (shader_uint i = 0; i < DIRECTIONAL_LIGHT_BUF_SIZE; ++i)
    dir.emplace_back(std::string{"dir_"} + std::to_string(i));
  lightOptionsNames.push_back(std::move(none));
  lightOptionsNames.push_back(std::move(point));
  lightOptionsNames.push_back(std::move(spot));
  lightOptionsNames.push_back(std::move(dir));
  return lightOptionsNames;
};

void WorldRenderer::drawGui()
{
  {
    ImGui::Begin("App");
    ImGui::Text("%.2fms (%dfps)", smoothedDt.getAvg() * 1e3f, int(1.f / smoothedDt.getAvg()));
    ImGui::Text(
      "Player pos: [%.3f, %.3f, %.3f]",
      constantsData.playerWorldPos.x,
      constantsData.playerWorldPos.y,
      constantsData.playerWorldPos.z);
    if (drawTerrain)
    {
      ImGui::Text(
        "Last toroidal update pos: [%.3f, %.3f]",
        constantsData.toroidalUpdatePlayerWorldPos.x,
        constantsData.toroidalUpdatePlayerWorldPos.y);
      ImGui::Text(
        "Toroidal offset: [%.3f, %.3f]",
        constantsData.toroidalOffset.x,
        constantsData.toroidalOffset.y);
    }
    if (directionalLightShadowsSettings.enable)
    {
      std::string text = fmt::format("Csm splits: [{}]{{{}", CSM_CASCADE_COUNT, mainCam.zNear);
      for (float split : std::span{
             reinterpret_cast<const float*>(mainViewParams.csmFrustumSplits), CSM_CASCADE_COUNT})
      {
        text.append(", ");
        text.append(std::to_string(split));
      }
      text.append("}");
      ImGui::Text("%s", text.c_str());
    }
    ImGui::End();
  }

  if (settingsGuiEnabled)
  {
    {
      ImGui::Begin("Lights");

      if (ImGui::Button("Turn on all directional lights"))
        setAllDirLightsIntensity(1.f);
      else if (ImGui::Button("Turn off all directional lights"))
        setAllDirLightsIntensity(0.f);
      if (ImGui::Button("Turn on all point lights"))
        setAllPointLightsIntensity(1.f);
      else if (ImGui::Button("Turn off all point lights"))
        setAllPointLightsIntensity(0.f);
      if (ImGui::Button("Turn on all spot lights"))
        setAllSpotLightsIntensity(1.f);
      else if (ImGui::Button("Turn off all spot lights"))
        setAllSpotLightsIntensity(0.f);

      // @TODO: not static
      static enum LType
      {
        NONE = 0,
        POINT = 1,
        SPOT = 2,
        DIRECTIONAL = 3
      } currentLightType = NONE;
      static shader_uint currentLightId = 0;

      // @TODO: bake it in somehow
      static auto lightOptionsNames = build_light_names_from_struct();

      auto lightName = [&](LType type, shader_uint id) {
        return lightOptionsNames[type][id].c_str();
      };
      auto curLightName = [&] { return lightName(currentLightType, currentLightId); };

      auto pointLightSettings = [this](int id) {
        auto& l = sceneMgr->lightsRW().pointLights[id];
        ImGui::SliderFloat3("position", (float*)&l.position, -15.f, 15.f);
        ImGui::NewLine();
        ImGui::SliderFloat("range", &l.range, 0.01f, 50.f);
        ImGui::NewLine();
        ImGui::ColorEdit3(
          "Meshes base color",
          (float*)&l.color,
          ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoInputs);
        ImGui::NewLine();
        ImGui::SliderFloat("intensity", &l.intensity, 0.01f, 4.f);
      };

      auto spotLightSettings = [this](int id) {
        auto& l = sceneMgr->lightsRW().spotLights[id];
        ImGui::SliderFloat3("position", (float*)&l.position, -15.f, 15.f);
        ImGui::NewLine();
        ImGui::SliderFloat3("direction", (float*)&l.direction, -1.f, 1.f);
        l.direction = glm::normalize(l.direction); // @TODO: can it be not here?
        ImGui::NewLine();
        ImGui::SliderFloat("range", &l.range, 0.01f, 50.f);
        ImGui::NewLine();
        ImGui::ColorEdit3(
          "Meshes base color",
          (float*)&l.color,
          ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoInputs);
        ImGui::NewLine();
        ImGui::SliderFloat("intensity", &l.intensity, 0.01f, 4.f);
        ImGui::NewLine();
        ImGui::SliderFloat("innerConeAngle", &l.innerConeAngle, 0.01f, 2.f * (float)M_PI);
        ImGui::NewLine();
        ImGui::SliderFloat("outerConeAngle", &l.outerConeAngle, 0.01f, 2.f * (float)M_PI);

        if (l.outerConeAngle < l.innerConeAngle + FLT_EPSILON)
          l.outerConeAngle = l.innerConeAngle + FLT_EPSILON;
      };

      auto directionalLightSettings = [this](int id) {
        auto& l = sceneMgr->lightsRW().directionalLights[id];
        ImGui::SliderFloat3("direction", (float*)&l.direction, -1.f, 1.f);
        l.direction = glm::normalize(l.direction); // @TODO: can it be not here?
        ImGui::NewLine();
        ImGui::ColorEdit3(
          "Meshes base color",
          (float*)&l.color,
          ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoInputs);
        ImGui::NewLine();
        ImGui::SliderFloat("intensity", &l.intensity, 0.01f, 4.f);
      };

      switch (currentLightType)
      {
      case POINT:
        pointLightSettings(currentLightId);
        break;
      case SPOT:
        spotLightSettings(currentLightId);
        break;
      case DIRECTIONAL:
        directionalLightSettings(currentLightId);
        break;
      default:
        break;
      }

      auto lightDropdown = [&](LType type, shader_uint count) {
        for (shader_uint i = 0; i < count; i++)
        {
          bool selected = currentLightType == type && currentLightId == i;
          if (ImGui::Selectable(lightName(type, i), selected))
          {
            currentLightType = type;
            currentLightId = i;
          }
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
      };

      if (ImGui::BeginCombo("##lights", curLightName()))
      {
        {
          bool selected = currentLightType == 0;
          if (ImGui::Selectable("none", selected))
            currentLightType = NONE;
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        lightDropdown(POINT, sceneMgr->getLights().pointLightsCount);
        lightDropdown(SPOT, sceneMgr->getLights().spotLightsCount);
        lightDropdown(DIRECTIONAL, sceneMgr->getLights().directionalLightsCount);

        ImGui::EndCombo();
      }

      ImGui::End();
    }
    {
      ImGui::Begin("Scene");

      ImGui::Checkbox("Draw scene", &drawScene);
      ImGui::Checkbox("Draw terrain", &drawTerrain);
      if (drawTerrain)
      {
        const bool prevDetailOn = drawTerrainSplattedDetail;
        const float prevTerrainNoiseRelHeightAmp = terrainNoiseRelHeightAmp;
        const float prevTerrainNoisePeriod = terrainNoisePeriod;
        ImGui::Checkbox("Draw terrain splatted details", &drawTerrainSplattedDetail);
        ImGui::SliderFloat("Terrain noise rel amplitude", &terrainNoiseRelHeightAmp, 0.f, 0.2f);
        ImGui::SliderFloat("Terrain noise period", &terrainNoisePeriod, 0.0001f, 2.f);
        if (
          prevDetailOn != drawTerrainSplattedDetail ||
          !shader_feq(prevTerrainNoiseRelHeightAmp, terrainNoiseRelHeightAmp) ||
          !shader_feq(prevTerrainNoisePeriod, terrainNoisePeriod))
        {
          queueClipmapInvalidation();
        }
      }
      ImGui::Checkbox("Draw vegetation", &drawVegetation);
      if (drawVegetation)
      {
        ImGui::Checkbox("Sort vegetation chunks", &sortVegChunks);
        ImGui::SliderFloat(
          "Vegetation draw disance",
          &vegetationRenderingDistance,
          0.01f,
          VEGETATION_GRID_EXTENT * VEGETATION_CHUNK_SIZE / 2.f - CLIMPAP_UPDATE_GRID_SIZE);
        ImGui::SliderFloat(
          "Vegetation draw dropoff disance",
          &vegetationRenderingDropoffDistance,
          0.01f,
          vegetationRenderingDistance);
      }
      ImGui::Checkbox("Show vegetation debug", &showGrassChunkDebug);
      ImGui::SliderFloat2("Wind direction", (float*)&windDirection, -1.f, 1.f);
      windDirection = glm::normalize(windDirection);
      ImGui::SliderFloat("Wind strengh", &windStrength, 0.f, 1.f);
      ImGui::Checkbox("Use SAT culling", &doSatCulling);
      ImGui::Checkbox("Perform Z Prepass", &zPrepass);
      ImGui::Checkbox("Enable skybox", &enableSkybox);
      if (enableSkybox)
      {
        ImGui::Checkbox("Use skybox for ambient", &useSkyboxForAmbient);
      }
      ImGui::ColorEdit3(
        "Ambient light coeff",
        (float*)&ambientCoeff,
        ImGuiColorEditFlags_PickerHueWheel | ImGuiColorEditFlags_NoInputs);
      auto prevWaterSettings = waterSettings;
      ImGui::Checkbox("Draw water", &waterSettings.enable);
      if (waterSettings.enable)
      {
        ImGui::SliderFloat("Fetch", &waterSettings.f, 0.f, 500.f);
        ImGui::SliderFloat("Average depth", &waterSettings.h, 0.f, 10000.f);
        ImGui::SliderFloat("Density", &waterSettings.rho, 800.f, 1200.f);
        ImGui::SliderFloat("Surface tension", &waterSettings.surfaceTension, 0.f, 0.5f);
        ImGui::SliderFloat("Window mps per app unit", &waterSettings.windUnitsToMps, 0.f, 100.f);
      }
      waterSettingsDirty |= waterSettings != prevWaterSettings;
      ImGui::Checkbox("Use SSAO", &useSsao);
      if (useSsao)
      {
        bool prevKHO = ssaoKernelHemisphereOnly;
        uint32_t prevSampleCount = ssaoTotalLimitSamples;
        uint32_t prevTemporalAccumBacklog = ssaoTemporalAccumBacklog;

        ImGui::Checkbox("SSAO kernel hemisphere only", &ssaoKernelHemisphereOnly);

        constexpr const char* SSAO_SC_NAMES[] = {"8", "16", "32", "64", "128"};
        constexpr uint32_t SSAO_SC_VALUES[] = {8, 16, 32, 64, 128};
        size_t curScId = size_t(
          std::find(std::begin(SSAO_SC_VALUES), std::end(SSAO_SC_VALUES), ssaoTotalLimitSamples) -
          std::begin(SSAO_SC_VALUES));
        if (ImGui::BeginCombo("SSAO kernel size", SSAO_SC_NAMES[curScId]))
        {
          for (size_t i = 0; i < ARRCNT(SSAO_SC_NAMES); i++)
          {
            bool selected = curScId == i;
            if (ImGui::Selectable(SSAO_SC_NAMES[i], selected))
            {
              curScId = i;
              ssaoTotalLimitSamples = SSAO_SC_VALUES[i];
            }
            if (selected)
              ImGui::SetItemDefaultFocus();
          }

          ImGui::EndCombo();
        }

        constexpr const char* SSAO_TA_NAMES[] = {"1", "2", "4", "8"};
        constexpr uint32_t SSAO_TA_VALUES[] = {1, 2, 4, 8};
        size_t curTaId = size_t(
          std::find(
            std::begin(SSAO_TA_VALUES), std::end(SSAO_TA_VALUES), ssaoTemporalAccumBacklog) -
          std::begin(SSAO_TA_VALUES));
        if (ImGui::BeginCombo("SSAO temporal accum backlog depth", SSAO_TA_NAMES[curTaId]))
        {
          for (size_t i = 0; i < ARRCNT(SSAO_TA_NAMES); i++)
          {
            bool selected = curTaId == i;
            if (ImGui::Selectable(SSAO_TA_NAMES[i], selected))
            {
              curTaId = i;
              ssaoTemporalAccumBacklog = SSAO_TA_VALUES[i];
            }
            if (selected)
              ImGui::SetItemDefaultFocus();
          }

          ImGui::EndCombo();
        }

        if (
          prevKHO != ssaoKernelHemisphereOnly || prevSampleCount != ssaoTotalLimitSamples ||
          prevTemporalAccumBacklog != ssaoTemporalAccumBacklog)
        {
          needRegenSsaoKernel = true;
        }
        ImGui::SliderFloat("SSAO kernel rad", &ssaoKernelRadius, 0.f, 5.f);
        ImGui::SliderFloat("SSAO bias", &ssaoBias, 0.f, 0.2f);
        ImGui::SliderFloat("SSAO power", &ssaoPower, 0.f, 5.f);
        if (ssaoTemporalAccumBacklog > 1)
        {
          ImGui::SliderFloat("SSAO ema coeff", &ssaoEmaCoeff, 0.f, 1.f);
          ImGui::SliderFloat(
            "SSAO depth rejection threshold", &ssaoDepthRejectionThreshold, 0.f, 1.f);
          ImGui::Checkbox("SSAO conservative caching", &ssaoConservariveTemporalCaching);
        }
        ImGui::Checkbox("Show SSAO debug", &showSsaoKernelDebug);
      }
      ImGui::Checkbox("Use tonemapping", &doTonemapping);
      if (doTonemapping)
      {
        if (ImGui::BeginCombo(
              "Tonemapper",
              TONEMAPPING_TECHNIQUE_NAMES[size_t(currentTonemappingTechnique)].data()))
        {
          for (size_t i = 0; i < TONEMAPPING_TECHNIQUE_COUNT; i++)
          {
            bool selected = currentTonemappingTechnique == TonemappingTechnique(i);
            if (ImGui::Selectable(TONEMAPPING_TECHNIQUE_NAMES[i].data(), selected))
              currentTonemappingTechnique = TonemappingTechnique(i);
            if (selected)
              ImGui::SetItemDefaultFocus();
          }

          ImGui::EndCombo();
        }

        if (currentTonemappingTechnique == TonemappingTechnique::HISTOGRAM_EQ)
        {
          ImGui::Checkbox("Use shared memory for tonemapping", &useSharedMemForTonemapping);

          float constW = 1.f - histEqTonemappingRegW - histEqTonemappingRefinedW;
          ImGui::SliderFloat("Regular bin W", &histEqTonemappingRegW, 0.f, 1.f);
          ImGui::SliderFloat("Refined bin W", &histEqTonemappingRefinedW, 0.f, 1.f);
          ImGui::SliderFloat("Constant W", &constW, 0.f, 1.f);

          histEqTonemappingRegW = glm::clamp(histEqTonemappingRegW, 0.f, 1.f);
          histEqTonemappingRefinedW =
            glm::clamp(histEqTonemappingRefinedW, 0.f, 1.f - histEqTonemappingRegW);

          float minAdmissibleLogLum =
            glm::log(histEqTonemappingMinAdmissibleLum + 1.f) / glm::log(10.f);
          float maxAdmissibleLogLum =
            glm::log(histEqTonemappingMaxAdmissibleLum + 1.f) / glm::log(10.f);
          ImGui::SliderFloat(
            "Min admissible lum (log10(+1))", &minAdmissibleLogLum, 0.f, 4.f - SHADER_EPSILON);
          ImGui::SliderFloat("Max admissible lum (log10(+1))", &maxAdmissibleLogLum, 0.f, 4.f);
          maxAdmissibleLogLum =
            glm::clamp(maxAdmissibleLogLum, minAdmissibleLogLum + SHADER_EPSILON, 4.f);

          histEqTonemappingMinAdmissibleLum = glm::exp(minAdmissibleLogLum * glm::log(10.f)) - 1.f;
          histEqTonemappingMaxAdmissibleLum = glm::exp(maxAdmissibleLogLum * glm::log(10.f)) - 1.f;
        }
        else if (currentTonemappingTechnique == TonemappingTechnique::ACES)
        {
          ImGui::SliderFloat("Hardcoded exposure", &acesExposure, 0.f, 64.f);
        }
      }

      ImGui::Checkbox("Use AA", &useAA);
      if (useAA)
      {
        if (ImGui::BeginCombo("Antialiaser", AA_TECHNIQUE_NAMES[size_t(currentAATechnique)].data()))
        {
          for (size_t i = 0; i < AA_TECHNIQUE_COUNT; i++)
          {
            bool selected = currentAATechnique == AATechnique(i);
            if (ImGui::Selectable(AA_TECHNIQUE_NAMES[i].data(), selected))
              currentAATechnique = AATechnique(i);
            if (selected)
              ImGui::SetItemDefaultFocus();
          }

          ImGui::EndCombo();
        }

        if (currentAATechnique == AATechnique::FXAA || currentAATechnique == AATechnique::FXAA311)
        {
          ImGui::Checkbox("Antialias in SRGB space", &fxaaAntialiasInSrgb);
        }
        else if (currentAATechnique == AATechnique::TAA)
        {
          constexpr const char* TAA_TA_NAMES[] = {"4", "8", "16"};
          constexpr uint32_t TAA_TA_VALUES[] = {4, 8, 16};
          size_t curTaId = size_t(
            std::find(std::begin(TAA_TA_VALUES), std::end(TAA_TA_VALUES), taaTemporalAccumBacklog) -
            std::begin(TAA_TA_VALUES));
          if (ImGui::BeginCombo("TAA temporal accum backlog depth", TAA_TA_NAMES[curTaId]))
          {
            for (size_t i = 0; i < ARRCNT(TAA_TA_NAMES); i++)
            {
              bool selected = curTaId == i;
              if (ImGui::Selectable(TAA_TA_NAMES[i], selected))
              {
                curTaId = i;
                taaTemporalAccumBacklog = TAA_TA_VALUES[i];
              }
              if (selected)
                ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
          }
          ImGui::SliderFloat("TAA ema coeff", &taaEmaCoeff, 0.f, 1.f);
          ImGui::Checkbox("TAA show debug", &showTaaPatternDebug);
        }
      }

      auto shadowsSettingsDropdown = [&](const char* name, ShadowsSettings& settings, bool& dirty) {
        std::string nameCap{name};
        nameCap[0] += 'A' - 'a';

        auto prevSettings = settings;

        ImGui::Checkbox(fmt::format("Enable {} light shadows", name).c_str(), &settings.enable);
        if (settings.enable)
        {
          if (ImGui::BeginCombo(
                fmt::format("({}) Shadowmap technique", name).c_str(),
                SHADOW_TECHNIQUE_NAMES[size_t(settings.technique)].data()))
          {
            for (size_t i = 0; i < SHADOW_TECHNIQUE_COUNT; i++)
            {
              bool selected = settings.technique == ShadowTechnique(i);
              if (ImGui::Selectable(SHADOW_TECHNIQUE_NAMES[i].data(), selected))
                settings.technique = ShadowTechnique(i);
              if (selected)
                ImGui::SetItemDefaultFocus();
            }

            ImGui::EndCombo();
          }

          ImGui::Checkbox(fmt::format("({}) Use depth bias", name).c_str(), &settings.depthBias);
          if (settings.depthBias)
          {
            ImGui::SliderFloat(
              fmt::format("({}) Depth bias const factor", name).c_str(),
              &settings.depthBiasConstantFactor,
              -12.f,
              12.f);
            ImGui::SliderFloat(
              fmt::format("({}) Depth bias clamp", name).c_str(),
              &settings.depthBiasClamp,
              -12.f,
              12.f);
            ImGui::SliderFloat(
              fmt::format("({}) Depth bias slope factor", name).c_str(),
              &settings.depthBiasSlopeFactor,
              -12.f,
              12.f);
          }

          ImGui::Checkbox(
            fmt::format("({}) Use front face culling", name).c_str(), &settings.frontFaceCull);
        }

        dirty |= settings != prevSettings;
      };

      shadowsSettingsDropdown("point", pointLightShadowsSettings, pointLightsSettingsDirty);
      shadowsSettingsDropdown("spot", spotLightShadowsSettings, spotLightsSettingsDirty);
      shadowsSettingsDropdown(
        "directional", directionalLightShadowsSettings, directionalLightsSettingsDirty);

      if (directionalLightShadowsSettings.enable)
      {
        ImGui::SliderFloat("CSM split lambda", &csmSplitLambda, 0.f, 1.f);
        ImGui::SliderFloat("CSM blending belt size", &csmBlendingBeltSize, 0.f, 0.6f);
        ImGui::SliderFloat("CSM shadow distance", &csmShadowDist, mainCam.zNear, mainCam.zFar);
        csmShadowDist = glm::clamp(csmShadowDist, mainCam.zNear, mainCam.zFar);
      }

      ImGui::Checkbox("Draw debug cascades", &drawCascadesInSolidColor);

      ImGui::Checkbox("Draw bounding boxes", &drawBboxes);
      ImGui::Checkbox("Wireframe", &wireframe);

      if (!terrain)
        drawTerrain = false;
      if (!vegetation)
        drawVegetation = false;
      if (!water)
        waterSettings.enable = false;

      if (ImGui::BeginCombo(
            "Debug texture view", currentDebugDrawer ? currentDebugDrawer->c_str() : "none"))
      {
        {
          bool selected = !currentDebugDrawer.has_value();
          if (ImGui::Selectable("none", selected))
            currentDebugDrawer.reset();
          if (selected)
            ImGui::SetItemDefaultFocus();
        }
        auto it = debugDrawers.begin();
        for (size_t i = 0; i < debugDrawers.size(); i++)
        {
          bool selected = currentDebugDrawer.has_value() && *currentDebugDrawer == it->first;
          if (ImGui::Selectable(it->first.c_str(), selected))
            currentDebugDrawer = it->first;
          if (selected)
            ImGui::SetItemDefaultFocus();

          ++it;
        }

        ImGui::EndCombo();
      }

      if (currentDebugDrawer)
        debugDrawers[*currentDebugDrawer].settings();

      if (cfg.useDebugConfig)
      {
        ImGui::NewLine();
        ImGui::Text("Debug config file : %s", cfg.debugConfigFile.c_str());
        if (ImGui::Button("Save config"))
          saveDebugConfig();
        if (ImGui::Button("Reload config"))
          loadDebugConfig();
      }

      ImGui::End();
    }
  }

  if (showGrassChunkDebug && terrain && terrain->sourceData.vegetationTypeCount > 0)
  {
    for (uint32_t i = 0; i < terrain->sourceData.vegetationTypeCount; ++i)
    {
      char buf[64];
      snprintf(buf, sizeof(buf), "Grass template debug %d", i);

      ImGui::SetNextWindowSize(ImVec2{400, 400}, ImGuiCond_Always);
      ImGui::Begin(buf);

      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* draw = ImGui::GetWindowDrawList();

      auto data = sceneMgr->getVegetationTemplateData();
      const auto& veg = terrain->sourceData.vegetationTypes[0];
      auto positions = data.subspan(veg.templateBufferOffset, veg.templateBufferSize);

      ImVec2 size = ImGui::GetContentRegionAvail();
      glm::vec2 sizeRatio = {size.x / VEGETATION_CHUNK_SIZE, size.y / VEGETATION_CHUNK_SIZE};

      for (const auto& pos : positions)
      {
        if (veg.radius <= veg.sparsenessRadius)
        {
          draw->AddCircleFilled(
            ImVec2{origin.x + pos.x * sizeRatio.x, origin.y + pos.y * sizeRatio.y},
            veg.sparsenessRadius * sizeRatio.x,
            IM_COL32(150, 150, 150, 175));
          draw->AddCircleFilled(
            ImVec2{origin.x + pos.x * sizeRatio.x, origin.y + pos.y * sizeRatio.y},
            veg.radius * sizeRatio.x,
            IM_COL32(100, 255, 100, 255));
        }
        else
        {
          draw->AddCircleFilled(
            ImVec2{origin.x + pos.x * sizeRatio.x, origin.y + pos.y * sizeRatio.y},
            veg.radius * sizeRatio.x,
            IM_COL32(100, 255, 100, 255));
          draw->AddCircleFilled(
            ImVec2{origin.x + pos.x * sizeRatio.x, origin.y + pos.y * sizeRatio.y},
            veg.sparsenessRadius * sizeRatio.x,
            IM_COL32(25, 64, 25, 255));
        }
      }

      ImGui::End();
    }
  }

  if (showSsaoKernelDebug)
  {
    auto drawSsaoKernelSlice = [&](float winsz, auto&& sx, auto&& sy, const char* tag) {
      ImGui::SetNextWindowSize(ImVec2{winsz, winsz}, ImGuiCond_Always);

      char buf[64];
      snprintf(buf, sizeof(buf), "SSAO kernel debug %s", tag);
      ImGui::Begin(buf);

      ImVec2 origin = ImGui::GetCursorScreenPos();
      ImDrawList* draw = ImGui::GetWindowDrawList();
      ImVec2 size = ImGui::GetContentRegionAvail();

      draw->AddCircle(
        ImVec2{origin.x + size.x * 0.5f, origin.y + size.y * 0.5f},
        0.49f * size.x,
        IM_COL32(255, 100, 100, 255));
      draw->AddLine(
        ImVec2{origin.x + size.x * 0.01f, origin.y + size.y * 0.5f},
        ImVec2{origin.x + size.x * 0.99f, origin.y + size.y * 0.5f},
        IM_COL32(255, 100, 100, 255));
      draw->AddLine(
        ImVec2{origin.x + size.x * 0.5f, origin.y + size.y * 0.5f - 0.49f * size.x},
        ImVec2{origin.x + size.x * 0.5f, origin.y + size.y * 0.5f + 0.49f * size.x},
        IM_COL32(255, 100, 100, 255));

      for (size_t i = 0; const auto& sample : std::span{constantsData.ssaoData.ssaoKernel}.subspan(
                           0, constantsData.ssaoLimitSamples))
      {
        const ImU32 colors[8] = {
          IM_COL32(100, 255, 100, 255),
          IM_COL32(255, 100, 100, 255),
          IM_COL32(100, 100, 255, 255),
          IM_COL32(255, 255, 100, 255),
          IM_COL32(100, 255, 255, 255),
          IM_COL32(255, 100, 255, 255),
          IM_COL32(255, 255, 255, 255),
          IM_COL32(100, 100, 100, 255)};
        size_t group = (i / (constantsData.ssaoLimitSamples / size_t(ssaoTemporalAccumBacklog))) %
          ARRCNT(colors);

        draw->AddCircleFilled(
          ImVec2{
            origin.x + size.x * (0.49f * sx(sample) + 0.5f),
            origin.y + size.y * (0.49f * sy(sample) + 0.5f)},
          0.01f * size.x,
          colors[group]);

        ++i;
      }

      ImGui::End();
    };

    drawSsaoKernelSlice(
      200, [](glm::vec3 v) { return v.x; }, [](glm::vec3 v) { return v.y; }, "xy");
    drawSsaoKernelSlice(
      200, [](glm::vec3 v) { return v.x; }, [](glm::vec3 v) { return v.z; }, "xz");
    drawSsaoKernelSlice(
      200, [](glm::vec3 v) { return v.y; }, [](glm::vec3 v) { return v.z; }, "yz");
  }

  if (showTaaPatternDebug)
  {
    ImGui::SetNextWindowSize(ImVec2{200, 200}, ImGuiCond_Always);
    ImGui::Begin("TAA subpixel jitter pattern");
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImVec2 size = ImGui::GetContentRegionAvail();
    ImVec2 center = origin + size * 0.5f;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    for (uint32_t i = 0; i < taaTemporalAccumBacklog; ++i)
    {
      const glm::vec2 uv = taaJitterSequence[i];
      draw->AddCircleFilled(
        ImVec2{center.x + uv.x * size.x, center.y + uv.y * size.y},
        0.02f * size.x,
        IM_COL32(150, 0, 0, 255));
    }
    ImGui::End();
  }
}

void WorldRenderer::createManagedImage(etna::Image& dst, etna::Image::CreateInfo&& ci)
{
  dst = create_image(std::move(ci));
  registerManagedImage(dst);
}

void WorldRenderer::registerManagedImage(
  const etna::Image& img, std::optional<std::string> name_override)
{
  debugDrawers[name_override ? *name_override : std::string{img.getName()}] = DebugDrawer{
    [&img, this](vk::CommandBuffer cb, vk::Image ti, vk::ImageView tiv) {
      quadRenderer->render(
        cb,
        ti,
        tiv,
        {{0, 0},
         {(resolution.y / 2) * img.getExtent().width / img.getExtent().height, resolution.y / 2}},
        img,
        defaultSampler,
        currentDebugTexLayer,
        currentDebugTexMip,
        currentDebugTexColorRange,
        currentDebugTexShowR,
        currentDebugTexShowG,
        currentDebugTexShowB,
        currentDebugTexShowA);
    },
    [&img, this] {
      ImGui::InputInt("Debug texture mip level", (int*)&currentDebugTexMip);
      ImGui::InputInt("Debug texture layer", (int*)&currentDebugTexLayer);
      ImGui::InputFloat2("Debug texture color range", &currentDebugTexColorRange.x);
      ImGui::Checkbox("R", &currentDebugTexShowR);
      ImGui::SameLine();
      ImGui::Checkbox("G", &currentDebugTexShowG);
      ImGui::SameLine();
      ImGui::Checkbox("B", &currentDebugTexShowB);
      ImGui::SameLine();
      ImGui::Checkbox("A", &currentDebugTexShowA);

      currentDebugTexColorRange.y =
        std::max(currentDebugTexColorRange.x, currentDebugTexColorRange.y);

      currentDebugTexMip =
        uint32_t(glm::clamp(int32_t(currentDebugTexMip), 0, int32_t(img.getMipLevelCount() - 1)));
      currentDebugTexLayer =
        uint32_t(glm::clamp(int32_t(currentDebugTexLayer), 0, int32_t(img.getLayerCount() - 1)));
    }};
}

static void validate_hist_tonemapping_coeffs(
  [[maybe_unused]] float reg,
  [[maybe_unused]] float refined,
  [[maybe_unused]] float min_lum,
  [[maybe_unused]] float max_lum)
{
  ETNA_ASSERT(reg >= 0.f && reg <= 1.f);
  ETNA_ASSERT(refined >= 0.f && refined <= 1.f);
  ETNA_ASSERT(reg + refined <= 1.f);
  ETNA_ASSERT(min_lum >= 0.f);
  ETNA_ASSERT(max_lum >= min_lum);
  ETNA_ASSERT(max_lum < 100.f);
}

void WorldRenderer::loadDebugConfig()
{
  auto readerMaybe = make_binfile_reader(cfg.debugConfigFile.c_str());
  if (!readerMaybe)
  {
    spdlog::warn(
      "Failed to load debug config from {}, using default settings", cfg.debugConfigFile);
    return;
  }

  auto& reader = *readerMaybe;
  auto verMaybe = reader.read<uint32_t>();

  if (!verMaybe)
  {
    spdlog::warn("Invalid debug config {}, using default settings", cfg.debugConfigFile);
    return;
  }
  else if (*verMaybe != cfg.debugConfigFileFormatVer)
  {
    spdlog::warn(
      "Loaded debug config with format ver {} from {}, while renderer requires {}, using default "
      "settings",
      *verMaybe,
      cfg.debugConfigFile,
      cfg.debugConfigFileFormatVer);
    return;
  }

  bool hasDebugDrawer = unwrap(reader.read<bool>());
  if (hasDebugDrawer)
  {
    uint32_t len = unwrap(reader.read<uint32_t>());
    std::string drawer{};
    drawer.resize(len, '\0');
    ETNA_ASSERT(drawer.length() == len);
    ETNA_VERIFY(reader.read(drawer.data(), drawer.length()));
    currentDebugDrawer.emplace(std::move(drawer));
  }
  else
  {
    currentDebugDrawer = std::nullopt;
  }

  currentDebugTexMip = unwrap(reader.read<uint32_t>());
  currentDebugTexLayer = unwrap(reader.read<uint32_t>());
  currentDebugTexColorRange = unwrap(reader.read<glm::vec2>());
  currentDebugTexShowR = unwrap(reader.read<bool>());
  currentDebugTexShowG = unwrap(reader.read<bool>());
  currentDebugTexShowB = unwrap(reader.read<bool>());
  currentDebugTexShowA = unwrap(reader.read<bool>());
  settingsGuiEnabled = unwrap(reader.read<bool>());
  drawBboxes = unwrap(reader.read<bool>());
  wireframe = unwrap(reader.read<bool>());
  drawScene = unwrap(reader.read<bool>());
  drawTerrain = unwrap(reader.read<bool>());
  drawTerrainSplattedDetail = unwrap(reader.read<bool>());
  drawVegetation = unwrap(reader.read<bool>());
  doSatCulling = unwrap(reader.read<bool>());
  enableSkybox = unwrap(reader.read<bool>());
  doTonemapping = unwrap(reader.read<bool>());
  useSharedMemForTonemapping = unwrap(reader.read<bool>());
  pointLightShadowsSettings = unwrap(reader.read<ShadowsSettings>());
  spotLightShadowsSettings = unwrap(reader.read<ShadowsSettings>());
  directionalLightShadowsSettings = unwrap(reader.read<ShadowsSettings>());
  drawCascadesInSolidColor = unwrap(reader.read<bool>());
  terrainNoiseRelHeightAmp = unwrap(reader.read<float>());
  terrainNoisePeriod = unwrap(reader.read<float>());
  vegetationRenderingDistance = unwrap(reader.read<float>());
  vegetationRenderingDropoffDistance = unwrap(reader.read<float>());
  windDirection = unwrap(reader.read<glm::vec2>());
  windStrength = unwrap(reader.read<float>());
  histEqTonemappingRegW = unwrap(reader.read<float>());
  histEqTonemappingRefinedW = unwrap(reader.read<float>());
  histEqTonemappingMinAdmissibleLum = unwrap(reader.read<float>());
  histEqTonemappingMaxAdmissibleLum = unwrap(reader.read<float>());
  acesExposure = unwrap(reader.read<float>());
  csmSplitLambda = unwrap(reader.read<float>());
  csmBlendingBeltSize = unwrap(reader.read<float>());
  csmShadowDist = unwrap(reader.read<float>());
  currentTonemappingTechnique = unwrap(reader.read<TonemappingTechnique>());
  zPrepass = unwrap(reader.read<bool>());
  sortVegChunks = unwrap(reader.read<bool>());
  useSsao = unwrap(reader.read<bool>());
  ambientCoeff = unwrap(reader.read<glm::vec3>());
  useSkyboxForAmbient = unwrap(reader.read<bool>());
  showGrassChunkDebug = unwrap(reader.read<bool>());
  showSsaoKernelDebug = unwrap(reader.read<bool>());
  ssaoKernelHemisphereOnly = unwrap(reader.read<bool>());
  ssaoKernelRadius = unwrap(reader.read<float>());
  ssaoBias = unwrap(reader.read<float>());
  ssaoTotalLimitSamples = unwrap(reader.read<uint32_t>());
  ssaoPower = unwrap(reader.read<float>());
  ssaoTemporalAccumBacklog = unwrap(reader.read<uint32_t>());
  ssaoEmaCoeff = unwrap(reader.read<float>());
  ssaoDepthRejectionThreshold = unwrap(reader.read<float>());
  ssaoConservariveTemporalCaching = unwrap(reader.read<bool>());
  currentAATechnique = unwrap(reader.read<AATechnique>());
  useAA = unwrap(reader.read<bool>());
  taaTemporalAccumBacklog = unwrap(reader.read<uint32_t>());
  showTaaPatternDebug = unwrap(reader.read<bool>());
  taaEmaCoeff = unwrap(reader.read<float>());
  waterSettings = unwrap(reader.read<WaterSettings>());

  ETNA_ASSERT(
    ssaoTotalLimitSamples == 4 || ssaoTotalLimitSamples == 8 || ssaoTotalLimitSamples == 16 ||
    ssaoTotalLimitSamples == 32 || ssaoTotalLimitSamples == 64 || ssaoTotalLimitSamples == 128);

  validate_hist_tonemapping_coeffs(
    histEqTonemappingRegW,
    histEqTonemappingRefinedW,
    histEqTonemappingMinAdmissibleLum,
    histEqTonemappingMaxAdmissibleLum);

  pointLightsSettingsDirty = true;
  spotLightsSettingsDirty = true;
  directionalLightsSettingsDirty = true;
  waterSettingsDirty = true;

  spdlog::info("Loaded debug config from {}", cfg.debugConfigFile.c_str());
}

void WorldRenderer::saveDebugConfig()
{
  auto writerMaybe = make_binfile_writer(cfg.debugConfigFile.c_str());
  if (!writerMaybe)
  {
    spdlog::warn("Failed to save debug config to {}", cfg.debugConfigFile);
    return;
  }

  auto& writer = *writerMaybe;

  ETNA_VERIFY(writer.write(cfg.debugConfigFileFormatVer));

  ETNA_VERIFY(writer.write(currentDebugDrawer.has_value()));
  if (currentDebugDrawer)
  {
    ETNA_VERIFY(writer.write(uint32_t(currentDebugDrawer->length())));
    ETNA_VERIFY(writer.write(currentDebugDrawer->c_str(), currentDebugDrawer->length()));
  }

  ETNA_VERIFY(writer.write(currentDebugTexMip));
  ETNA_VERIFY(writer.write(currentDebugTexLayer));
  ETNA_VERIFY(writer.write(currentDebugTexColorRange));
  ETNA_VERIFY(writer.write(currentDebugTexShowR));
  ETNA_VERIFY(writer.write(currentDebugTexShowG));
  ETNA_VERIFY(writer.write(currentDebugTexShowB));
  ETNA_VERIFY(writer.write(currentDebugTexShowA));
  ETNA_VERIFY(writer.write(settingsGuiEnabled));
  ETNA_VERIFY(writer.write(drawBboxes));
  ETNA_VERIFY(writer.write(wireframe));
  ETNA_VERIFY(writer.write(drawScene));
  ETNA_VERIFY(writer.write(drawTerrain));
  ETNA_VERIFY(writer.write(drawTerrainSplattedDetail));
  ETNA_VERIFY(writer.write(drawVegetation));
  ETNA_VERIFY(writer.write(doSatCulling));
  ETNA_VERIFY(writer.write(enableSkybox));
  ETNA_VERIFY(writer.write(doTonemapping));
  ETNA_VERIFY(writer.write(useSharedMemForTonemapping));
  ETNA_VERIFY(writer.write(pointLightShadowsSettings));
  ETNA_VERIFY(writer.write(spotLightShadowsSettings));
  ETNA_VERIFY(writer.write(directionalLightShadowsSettings));
  ETNA_VERIFY(writer.write(drawCascadesInSolidColor));
  ETNA_VERIFY(writer.write(terrainNoiseRelHeightAmp));
  ETNA_VERIFY(writer.write(terrainNoisePeriod));
  ETNA_VERIFY(writer.write(vegetationRenderingDistance));
  ETNA_VERIFY(writer.write(vegetationRenderingDropoffDistance));
  ETNA_VERIFY(writer.write(windDirection));
  ETNA_VERIFY(writer.write(windStrength));
  ETNA_VERIFY(writer.write(histEqTonemappingRegW));
  ETNA_VERIFY(writer.write(histEqTonemappingRefinedW));
  ETNA_VERIFY(writer.write(histEqTonemappingMinAdmissibleLum));
  ETNA_VERIFY(writer.write(histEqTonemappingMaxAdmissibleLum));
  ETNA_VERIFY(writer.write(acesExposure));
  ETNA_VERIFY(writer.write(csmSplitLambda));
  ETNA_VERIFY(writer.write(csmBlendingBeltSize));
  ETNA_VERIFY(writer.write(csmShadowDist));
  ETNA_VERIFY(writer.write(currentTonemappingTechnique));
  ETNA_VERIFY(writer.write(zPrepass));
  ETNA_VERIFY(writer.write(sortVegChunks));
  ETNA_VERIFY(writer.write(useSsao));
  ETNA_VERIFY(writer.write(ambientCoeff));
  ETNA_VERIFY(writer.write(useSkyboxForAmbient));
  ETNA_VERIFY(writer.write(showGrassChunkDebug));
  ETNA_VERIFY(writer.write(showSsaoKernelDebug));
  ETNA_VERIFY(writer.write(ssaoKernelHemisphereOnly));
  ETNA_VERIFY(writer.write(ssaoKernelRadius));
  ETNA_VERIFY(writer.write(ssaoBias));
  ETNA_VERIFY(writer.write(ssaoTotalLimitSamples));
  ETNA_VERIFY(writer.write(ssaoPower));
  ETNA_VERIFY(writer.write(ssaoTemporalAccumBacklog));
  ETNA_VERIFY(writer.write(ssaoEmaCoeff));
  ETNA_VERIFY(writer.write(ssaoDepthRejectionThreshold));
  ETNA_VERIFY(writer.write(ssaoConservariveTemporalCaching));
  ETNA_VERIFY(writer.write(currentAATechnique));
  ETNA_VERIFY(writer.write(useAA));
  ETNA_VERIFY(writer.write(taaTemporalAccumBacklog));
  ETNA_VERIFY(writer.write(showTaaPatternDebug));
  ETNA_VERIFY(writer.write(taaEmaCoeff));
  ETNA_VERIFY(writer.write(waterSettings));

  spdlog::info("Saved debug config to {}", cfg.debugConfigFile.c_str());
}

void WorldRenderer::setAllDirLightsIntensity(float val)
{
  for (size_t i = 0; i < sceneMgr->getLights().directionalLightsCount; ++i)
    sceneMgr->lightsRW().directionalLights[i].intensity = val;
}
void WorldRenderer::setAllPointLightsIntensity(float val)
{
  for (size_t i = 0; i < sceneMgr->getLights().pointLightsCount; ++i)
    sceneMgr->lightsRW().pointLights[i].intensity = val;
}
void WorldRenderer::setAllSpotLightsIntensity(float val)
{
  for (size_t i = 0; i < sceneMgr->getLights().spotLightsCount; ++i)
    sceneMgr->lightsRW().spotLights[i].intensity = val;
}

void WorldRenderer::generateSsaoKernel(std::span<glm::vec4> out_samples) const
{
  // Spiral as in SAO, uniform z-angle dist
  std::uniform_real_distribution<float> randomFloats(0.0, 1.f);
  def_rng generator(1);
  auto alphaFromI = [&](auto i) { return (float(i) + 0.5f) / float(out_samples.size()); };
  float alphaNormalization = 1.f / alphaFromI(out_samples.size() - 1);
  const float spiralRevolutionFactor = 1.3f;
  for (uint32_t i = 0; i < out_samples.size(); ++i)
  {
    float alpha = alphaFromI(i);
    float r = alpha * alphaNormalization;
    float phiFlat = 2.f * float(M_PI) * alpha * spiralRevolutionFactor;
    float phiVert = 2.f * float(M_PI) * randomFloats(generator);
    float x = cosf(phiFlat) * cosf(phiVert);
    float y = sinf(phiFlat) * cosf(phiVert);
    float z = sinf(phiVert);

    uint32_t interlacedIndex = (i % 4) * (uint32_t(out_samples.size()) / 4) + (i / 4);
    out_samples[interlacedIndex] = r * glm::vec4(x, y, z, 0.f);
  }
  if (ssaoKernelHemisphereOnly)
  {
    for (auto& sample : out_samples)
      sample.z = glm::abs(sample.z);
  }
}

void WorldRenderer::generateSsaoKernelRotations(std::span<glm::vec4> out_rotations) const
{
  std::uniform_real_distribution<float> randomFloats(0.0, 1.0);
  def_rng generator(1);
  for (uint32_t i = 0; i < out_rotations.size(); ++i)
  {
    glm::vec4 rotvPair(
      randomFloats(generator) * 2.f - 1.f,
      randomFloats(generator) * 2.f - 1.f,
      randomFloats(generator) * 2.f - 1.f,
      randomFloats(generator) * 2.f - 1.f);
    out_rotations[i] = rotvPair;
  }
}

static void generate_random_trash(std::span<glm::vec2> out_samples)
{
  std::uniform_real_distribution<float> randomFloats(-0.5, 0.5);
  def_rng generator(42);
  std::ranges::copy(
    std::views::iota(0) //
      | std::views::transform([&](auto) {
          return glm::vec2{randomFloats(generator), randomFloats(generator)};
        }) //
      | std::views::take(out_samples.size()),
    out_samples.begin());
}

static void generate_halton(std::span<glm::vec2> out_samples, const int basex, const int basey)
{
  std::ranges::copy(
    std::views::iota(0) //
      | std::views::transform([&, nx = 0, dx = 1, ny = 0, dy = 1](auto) mutable {
          auto apply = [](int& n, int& d, int base) {
            int x = d - n;
            if (x == 1)
            {
              n = 1;
              d *= base;
            }
            else
            {
              int y = d / base;
              while (x <= y)
                y /= base;
              n = (base + 1) * y - x;
            }
            return float(n) / float(d);
          };
          return glm::vec2(apply(nx, dx, basex), apply(ny, dy, basey)) - 0.5f;
        }) //
      | std::views::take(out_samples.size()),
    out_samples.begin());
}

static void generate_r2(std::span<glm::vec2> out_samples, const float g)
{
  float a1 = 1.f / g;
  float a2 = 1.f / (g * g);
  std::ranges::copy(
    std::views::iota(0) //
      | std::views::transform([&](auto i) {
          float n = float(i + 1);
          float sink;
          return glm::vec2(modff(0.5f + a1 * n, &sink), modff(0.5f + a2 * n, &sink)) - 0.5f;
        }) //
      | std::views::take(out_samples.size()),
    out_samples.begin());
}

static void debias_sequence(std::span<glm::vec2> inout_samples)
{
  glm::vec2 center(0.f, 0.f);
  for (const auto& sample : inout_samples)
    center += sample;
  center /= float(inout_samples.size());
  for (auto& sample : inout_samples)
    sample -= center;
}

void WorldRenderer::generateTaaJitterSequence(std::span<glm::vec2> out_jitters) const
{
  generate_halton(out_jitters, 2, 3);
  // generate_r2(out_jitters, 1.32471795724474602596f);
  (void)&generate_r2;
  (void)&generate_halton;
  (void)&generate_random_trash;
  debias_sequence(out_jitters);
}

glm::vec2 WorldRenderer::getCurFrameTaaUvJitter() const
{
  return taaJitterSequence[wc.batchIndex() % taaTemporalAccumBacklog] / glm::vec2(resolution);
}
