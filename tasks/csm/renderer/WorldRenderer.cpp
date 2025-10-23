#define _USE_MATH_DEFINES

#include "WorldRenderer.hpp"

#include <render_components/HistogramEqTonemapper.hpp>
#include <render_components/ReinhardTonemapper.hpp>
#include <render_components/AcesTonemapper.hpp>

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

WorldRenderer::MeshPipeline::MeshPipeline(
  etna::PipelineManager& pipeman,
  const char* prog_name,
  const char* vertex_prog_name,
  const etna::GraphicsPipeline::CreateInfo& ci)
{
  auto nci = ci;
  pipelines[size_t(SceneRenderingPass::COLOR)][size_t(DepthFlavour::NORMAL)] =
    pipeman.createGraphicsPipeline(prog_name, nci);
  nci.depthConfig.depthCompareOp = vk::CompareOp::eGreaterOrEqual;
  pipelines[size_t(SceneRenderingPass::COLOR)][size_t(DepthFlavour::REVERSE)] =
    pipeman.createGraphicsPipeline(prog_name, nci);
  programs[size_t(SceneRenderingPass::COLOR)].emplace(etna::get_shader_program(prog_name));

  {
    auto wci = ci;
    wci.rasterizationConfig.polygonMode = vk::PolygonMode::eLine;
    pipelines[size_t(SceneRenderingPass::WIRE_COLOR)][size_t(DepthFlavour::NORMAL)] =
      pipeman.createGraphicsPipeline(prog_name, wci);
    wci.depthConfig.depthCompareOp = vk::CompareOp::eGreaterOrEqual;
    pipelines[size_t(SceneRenderingPass::WIRE_COLOR)][size_t(DepthFlavour::REVERSE)] =
      pipeman.createGraphicsPipeline(prog_name, wci);
    programs[size_t(SceneRenderingPass::WIRE_COLOR)].emplace(etna::get_shader_program(prog_name));
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
      pipeman.createGraphicsPipeline(vertex_prog_name, sci);
    sci.depthConfig.depthCompareOp = vk::CompareOp::eGreaterOrEqual;
    pipelines[size_t(SceneRenderingPass::SHADOW)][size_t(DepthFlavour::REVERSE)] =
      pipeman.createGraphicsPipeline(vertex_prog_name, sci);
    programs[size_t(SceneRenderingPass::SHADOW)].emplace(
      etna::get_shader_program(vertex_prog_name));
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
      pipeman.createGraphicsPipeline(vertex_prog_name, sci);
    sci.depthConfig.depthCompareOp = vk::CompareOp::eGreaterOrEqual;
    pipelines[size_t(SceneRenderingPass::SHADOW_FRONT_CULLED)][size_t(DepthFlavour::REVERSE)] =
      pipeman.createGraphicsPipeline(vertex_prog_name, sci);
    programs[size_t(SceneRenderingPass::SHADOW_FRONT_CULLED)].emplace(
      etna::get_shader_program(vertex_prog_name));
  }
}

WorldRenderer::WorldRenderer(const etna::GpuWorkCount& wc, const Config& config)
  : sceneMgr{std::make_unique<SceneManager>()}
  , wc{wc}
  , cfg{config}
{
  registerViewContextManager();

  registerTonemapper<HistogramEqTonemapper>(TonemappingTechnique::HISTOGRAM_EQ);
  registerTonemapper<ReinhardTonemapper>(TonemappingTechnique::REINHARD);
  registerTonemapper<AcesTonemapper>(TonemappingTechnique::ACES);

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

  defaultSampler = etna::Sampler(
    etna::Sampler::CreateInfo{
      .name = "default_sampler", .minLod = 0.f, .maxLod = VK_LOD_CLAMP_NONE});

  constants.emplace(wc, [](size_t) {
    return create_buffer(
      etna::Buffer::CreateInfo{
        .size = sizeof(constantsData),
        .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
        .name = "constants"});
  });
  lights.emplace(wc, [](size_t) {
    return create_buffer(
      etna::Buffer::CreateInfo{
        .size = sizeof(sceneMgr->getLights()),
        .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
        .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
        .name = "lights"});
  });
  constants->iterate([](auto& buf) { buf.map(); });
  lights->iterate([](auto& buf) { buf.map(); });
  prevLights = {};

  lightMatricesBuf = create_buffer(
    etna::Buffer::CreateInfo{
      .size = sizeof(LightMatrices),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "light_matrices"});

  stubUniBuffer = create_buffer(
    etna::Buffer::CreateInfo{
      .size = 16,
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
      .name = "stub_uniform"});
  stubStorageBuffer = create_buffer(
    etna::Buffer::CreateInfo{
      .size = 16,
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = "stub_storage"});

  for (auto& component : rcomponents)
    component->allocateResources(resolution);
}

void WorldRenderer::loadScene(std::filesystem::path path)
{
  // @TODO: make recallable, i.e. implement cleanup

  sceneMgr->selectScene(path, cfg.testMultiplexScene ? cfg.testMultiplexing : SceneMultiplexing{});

  mainViewContext.emplace(viewCtxMgr->alloc("main"));

  pointLightViews.reserve(sceneMgr->getLights().pointLightsCount);
  spotLightViews.reserve(sceneMgr->getLights().spotLightsCount);
  directionalLightCascadeViews.reserve(sceneMgr->getLights().directionalLightsCount);

  for (size_t i = 0; i < sceneMgr->getLights().pointLightsCount; ++i)
  {
    pointLightViews.emplace_back(array_make<ViewContext, 6>([this, i] {
      return viewCtxMgr->alloc(fmt::format("point{}", i).c_str());
    }));
  }
  for (size_t i = 0; i < sceneMgr->getLights().spotLightsCount; ++i)
  {
    spotLightViews.emplace_back(viewCtxMgr->alloc(fmt::format("spot{}", i).c_str()));
  }
  for (size_t i = 0; i < sceneMgr->getLights().directionalLightsCount; ++i)
  {
    directionalLightCascadeViews.emplace_back(array_make<ViewContext, CSM_CASCADE_COUNT>([this, i] {
      return viewCtxMgr->alloc(fmt::format("dir{}", i).c_str());
    }));
  }

  if (sceneMgr->hasTerrain())
  {
    spdlog::info("JB_terrain: terrain loaded!");

    terrain.emplace(TerrainRenderingData{});

    memcpy(&terrain->sourceData, &sceneMgr->getTerrainData(), sizeof(sceneMgr->getTerrainData()));
    terrain->source = create_buffer(
      etna::Buffer::CreateInfo{
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

    terrain->clipmapSampler = etna::Sampler(
      etna::Sampler::CreateInfo{
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

    terrain->chunkHeightBoundsBuf = create_buffer(
      etna::Buffer::CreateInfo{
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

  if (sceneMgr->hasSkybox())
  {
    spdlog::info("JB_skybox: skybox loaded!");

    skybox.emplace(SkyboxRenderingData{});

    memcpy(&skybox->sourceData, &sceneMgr->getSkyboxData(), sizeof(sceneMgr->getSkyboxData()));
    skybox->source = create_buffer(
      etna::Buffer::CreateInfo{
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
    const auto& tex = sceneMgr->getTextures()[i];
    etna::Image::ViewParams vps{};
    if (tex.getCreationFlags() & vk::ImageCreateFlagBits::eCubeCompatible)
      vps.type = vk::ImageViewType::eCube;
    texBindings.emplace_back(
      etna::Binding{
        0, tex.genBinding({}, vk::ImageLayout::eShaderReadOnlyOptimal, vps), uint32_t(i)});
    registerManagedImage(tex, fmt::format("bindless_tex_{}[{}]", i, tex.getName()));
  }
  for (size_t i = 0; i < sceneMgr->getSamplers().size(); ++i)
  {
    const auto& smp = sceneMgr->getSamplers()[i];
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
  etna::create_program("static_mesh_shadow", {RENDERER_SHADERS_ROOT "static_mesh_shadow.vert.spv"});
  etna::create_program(
    "terrain_mesh",
    {RENDERER_SHADERS_ROOT "terrain_mesh.frag.spv",
     RENDERER_SHADERS_ROOT "terrain_mesh.vert.spv",
     RENDERER_SHADERS_ROOT "terrain_mesh.tesc.spv",
     RENDERER_SHADERS_ROOT "terrain_mesh.tese.spv"});
  etna::create_program(
    "terrain_mesh_shadow",
    {RENDERER_SHADERS_ROOT "terrain_mesh.vert.spv",
     RENDERER_SHADERS_ROOT "terrain_mesh.tesc.spv",
     RENDERER_SHADERS_ROOT "terrain_mesh_shadow.tese.spv"});
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

  for (auto& component : rcomponents)
    component->loadShaders();
}

void WorldRenderer::setupPipelines(vk::Format swapchain_format)
{
  etna::VertexShaderInputDescription sceneVertexInputDesc{
    .bindings = {etna::VertexShaderInputDescription::Binding{
      .byteStreamDescription = sceneMgr->getVertexFormatDescription(),
    }},
  };

  auto& pipelineManager = etna::get_context().getPipelineManager();

  // @TODO: compactify
  auto meshPipelineCreateInfo =
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
           },
         .logicOp = vk::LogicOp::eSet},
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = // @TODO: save these into vars
          {vk::Format::eR32G32B32A32Sfloat,
           vk::Format::eR32G32B32A32Sfloat,
           vk::Format::eR32G32B32A32Sfloat},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },
    };
  auto terrainPipelineCreateInfo =
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
           },
         .logicOp = vk::LogicOp::eSet},
      .fragmentShaderOutput =
        {
          .colorAttachmentFormats = // @TODO: save these into vars
          {vk::Format::eR32G32B32A32Sfloat,
           vk::Format::eR32G32B32A32Sfloat,
           vk::Format::eR32G32B32A32Sfloat},
          .depthAttachmentFormat = vk::Format::eD32Sfloat,
        },

    };

  staticMeshPipeline.emplace(
    pipelineManager, "static_mesh", "static_mesh_shadow", meshPipelineCreateInfo);
  terrainMeshPipeline.emplace(
    pipelineManager, "terrain_mesh", "terrain_mesh_shadow", terrainPipelineCreateInfo);

  generateClipmapPipeline = pipelineManager.createComputePipeline("clipmap_gen", {});
  resetTerrainChunkHeightBoundsPipeline =
    pipelineManager.createComputePipeline("reset_terrain_chunk_height_bounds", {});
  generateTerrainChunkHeightBoundsPipeline =
    pipelineManager.createComputePipeline("generate_terrain_chunk_height_bounds", {});
  transferTerrainChunkHeightBoundsPipeline =
    pipelineManager.createComputePipeline("transfer_terrain_chunk_height_bounds", {});
  transferLightMatsPipeline = pipelineManager.createComputePipeline("transfer_light_mats", {});

  gbufferResolver = std::make_unique<PostfxRenderer>(PostfxRenderer::CreateInfo{
    "gbuffer_resolve",
    RENDERER_SHADERS_ROOT "gbuffer_resolve.frag.spv",
    vk::Format::eR32G32B32A32Sfloat,
    {resolution.x, resolution.y}});

  bboxRenderer = std::make_unique<BboxRenderer>(BboxRenderer::CreateInfo{swapchain_format});
  quadRenderer = std::make_unique<QuadRenderer>(QuadRenderer::CreateInfo{swapchain_format});

  for (auto& component : rcomponents)
    component->setupPipelines(swapchain_format, debugDrawers);
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

    constantsData.useTonemapping = doTonemapping;
    constantsData.useSharedMemForTonemapping = useSharedMemForTonemapping;
    constantsData.histEqTonemappingRegW = histEqTonemappingRegW;
    constantsData.histEqTonemappingRefinedW = histEqTonemappingRefinedW;
    constantsData.histEqTonemappingMinAdmissibleLum = histEqTonemappingMinAdmissibleLum;
    constantsData.histEqTonemappingMaxAdmissibleLum = histEqTonemappingMaxAdmissibleLum;
    constantsData.acesExposure = acesExposure;

    constantsData.csmSplitLambda = csmSplitLambda;
    constantsData.csmBlendingBeltSize = csmBlendingBeltSize;
  }

  {
    auto& lights = sceneMgr->lightsRW();
    const auto mainViewParams =
      view_params_for_cam(mainCam, aspect(), false, false, csmSplitLambda, csmShadowDist);
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

      for (auto& dirl : std::span{lights.directionalLights, lights.directionalLightsCount})
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
    return p == SceneRenderingPass::COLOR || p == SceneRenderingPass::WIRE_COLOR;
  };

  srpi.vctx->update(srpi.vparams);
  viewCtxMgr->cullForView(cmd_buf, *srpi.vctx, srpi.vparams, constants->get());

  if (srpi.vparams.needReverseZ)
    srpi.rtargetInfo.depthAttachment.clearDepthStencilValue = {0.f, 0};

  {
    ETNA_PROFILE_GPU(cmd_buf, renderScene);

    auto sceneDset = [&, this]() -> std::optional<etna::DescriptorSet> {
      if (drawScene)
      {
        return etna::create_descriptor_set(
          staticMeshPipeline->getProg(srpi.pass).getDescriptorLayoutId(0),
          cmd_buf,
          {etna::Binding{0, sceneMgr->getInstanceMatricesBuf().genBinding()},
           etna::Binding{1, srpi.vctx->culledInstancesBuf.genBinding()},
           etna::Binding{9, srpi.vctx->viewParamsBuf.get().genBinding()},
           etna::Binding{10, srpi.vctx->viewDataBuf.genBinding()}});
      }
      else
      {
        return std::nullopt;
      }
    }();
    auto terrainDset = [&, this]() -> std::optional<etna::DescriptorSet> {
      if (terrain && drawTerrain)
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

    etna::RenderTargetState renderTargets{cmd_buf, srpi.rtargetInfo};

    cmd_buf.setDepthBiasEnable(vk::Bool32(srpi.depthBias));
    if (srpi.depthBias)
    {
      cmd_buf.setDepthBias(
        srpi.depthBiasConstantFactor, srpi.depthBiasClamp, srpi.depthBiasSlopeFactor);
    }

    if (drawScene)
    {
      ETNA_PROFILE_GPU(cmd_buf, sceneMeshes);

      const auto& pipe = staticMeshPipeline->get(srpi.pass, srpi.vparams.needReverseZ);
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

    if (terrain && drawTerrain)
    {
      ETNA_PROFILE_GPU(cmd_buf, terrain);

      const auto& pipe = terrainMeshPipeline->get(srpi.pass, srpi.vparams.needReverseZ);

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
  }
}

void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

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

  memcpy(constants->get().data(), &constantsData, sizeof(constantsData));
  memcpy(lights->get().data(), &sceneMgr->getLights(), sizeof(sceneMgr->getLights()));

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

      const auto& lights = sceneMgr->getLights();

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
        for (size_t i = 0;
             const auto& point : std::span{lights.pointLights, lights.pointLightsCount})
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
               .vctx = &pointLightViews[i][j],
               .vparams = view_params_for_cam(
                 cam, 1.f, true, false), // @TODO: reverse depth is broken on point lights
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

        for (size_t i = 0; const auto& spot : std::span{lights.spotLights, lights.spotLightsCount})
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
          cam.fov = spot.outerConeAngle * 180.f / M_PI;
          cam.zNear = 0.001f;
          cam.zFar = spot.range + 0.001f;

          renderScene(
            cmd_buf,
            {.pass = spotLightShadowsSettings.frontFaceCull
               ? SceneRenderingPass::SHADOW_FRONT_CULLED
               : SceneRenderingPass::SHADOW,
             .vctx = &spotLightViews[i],
             .vparams = view_params_for_cam(
               cam, 1.f, true, false), // @TODO: reverse depth is broken on spot lights
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
             const auto& dirl : std::span{lights.directionalLights, lights.directionalLightsCount})
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
               .vctx = &directionalLightCascadeViews[i][j],
               .vparams = view_params_for_cam(cam, xExt, yExt, true),
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

    {
      ETNA_PROFILE_GPU(cmd_buf, deferredGpass);

      renderScene(
        cmd_buf,
        {.pass = wireframe ? SceneRenderingPass::WIRE_COLOR : SceneRenderingPass::COLOR,
         .vctx = &mainViewContext.value(),
         .vparams =
           view_params_for_cam(mainCam, aspect(), false, true, csmSplitLambda, csmShadowDist),
         .rtargetInfo = {
           {{0, 0}, {resolution.x, resolution.y}},
           {{.image = gbufAlbedo.get(), .view = gbufAlbedo.getView({})},
            {.image = gbufMaterial.get(), .view = gbufMaterial.getView({})},
            {.image = gbufNormal.get(), .view = gbufNormal.getView({})}},
           {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})}}});
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
           7,
           mainViewDepth.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
         etna::Binding{8, constants->get().genBinding()},
         etna::Binding{9, mainViewContext->viewParamsBuf.get().genBinding()},
         etna::Binding{10, mainViewContext->viewDataBuf.genBinding()},
         etna::Binding{11, (skybox ? skybox->source : stubUniBuffer).genBinding()}});

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

      tonemapperComps[size_t(currentTonemappingTechnique)]->tonemap(
        cmd_buf, target_image, target_image_view, hdrTarget, defaultSampler, constants->get());
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
    ImGui::Text("%.2fms (%dfps)", dt * 1e3f, int(1.f / dt));
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
      std::string text = std::format("Csm splits: [{}]{{{}", CSM_CASCADE_COUNT, mainCam.zNear);
      const ViewParams mainCamParams =
        view_params_for_cam(mainCam, aspect(), false, false, csmSplitLambda, csmShadowDist);
      for (float split : std::span{
             reinterpret_cast<const float*>(mainCamParams.csmFrustumSplits), CSM_CASCADE_COUNT})
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
        const bool prevTerrainNoiseRelHeightAmp = terrainNoiseRelHeightAmp;
        const bool prevTerrainNoisePeriod = terrainNoisePeriod;
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
      ImGui::Checkbox("Use SAT culling", &doSatCulling);
      ImGui::Checkbox("Enable skybox", &enableSkybox);
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

      // @TODO: text
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
}

void WorldRenderer::createManagedImage(etna::Image& dst, etna::Image::CreateInfo&& ci)
{
  dst = create_image(std::move(ci));
  registerManagedImage(dst);
}

void WorldRenderer::registerManagedImage(
  const etna::Image& img, std::optional<std::string> name_override)
{
  debugDrawers.emplace(
    name_override ? *name_override : std::string{img.getName()},
    DebugDrawer{
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
      }});
}

static void validate_hist_tonemapping_coeffs(float reg, float refined, float min_lum, float max_lum)
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
  histEqTonemappingRegW = unwrap(reader.read<float>());
  histEqTonemappingRefinedW = unwrap(reader.read<float>());
  histEqTonemappingMinAdmissibleLum = unwrap(reader.read<float>());
  histEqTonemappingMaxAdmissibleLum = unwrap(reader.read<float>());
  acesExposure = unwrap(reader.read<float>());
  csmSplitLambda = unwrap(reader.read<float>());
  csmBlendingBeltSize = unwrap(reader.read<float>());
  csmShadowDist = unwrap(reader.read<float>());
  currentTonemappingTechnique = unwrap(reader.read<TonemappingTechnique>());

  validate_hist_tonemapping_coeffs(
    histEqTonemappingRegW,
    histEqTonemappingRefinedW,
    histEqTonemappingMinAdmissibleLum,
    histEqTonemappingMaxAdmissibleLum);

  pointLightsSettingsDirty = true;
  spotLightsSettingsDirty = true;
  directionalLightsSettingsDirty = true;

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
  ETNA_VERIFY(writer.write(histEqTonemappingRegW));
  ETNA_VERIFY(writer.write(histEqTonemappingRefinedW));
  ETNA_VERIFY(writer.write(histEqTonemappingMinAdmissibleLum));
  ETNA_VERIFY(writer.write(histEqTonemappingMaxAdmissibleLum));
  ETNA_VERIFY(writer.write(acesExposure));
  ETNA_VERIFY(writer.write(csmSplitLambda));
  ETNA_VERIFY(writer.write(csmBlendingBeltSize));
  ETNA_VERIFY(writer.write(csmShadowDist));
  ETNA_VERIFY(writer.write(currentTonemappingTechnique));

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
