#define _USE_MATH_DEFINES

#include "WorldRenderer.hpp"

#include <render_utils/PostfxRenderer.hpp>
#include <utils/Common.hpp>
#include <utils/Bitstream.hpp>
#include <tonemapping.h>

#include <etna/GlobalContext.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/RenderTargetStates.hpp>
#include <etna/Profiling.hpp>
#include <etna/Etna.hpp>
#include <glm/ext.hpp>
#include <imgui.h>

#include <cassert>
#include <memory>
#include <vector>

WorldRenderer::MeshPipeline::MeshPipeline(
  etna::PipelineManager& pipeman,
  const char* prog_name,
  const etna::GraphicsPipeline::CreateInfo& ci)
  : mainPipeline{pipeman.createGraphicsPipeline(prog_name, ci)}
{
  auto wci = ci;
  wci.rasterizationConfig.polygonMode = vk::PolygonMode::eLine;
  wireframePipeline = pipeman.createGraphicsPipeline(prog_name, wci);
}

WorldRenderer::WorldRenderer(const etna::GpuWorkCount& wc, const Config& config)
  : sceneMgr{std::make_unique<SceneManager>()}
  , wc{wc}
  , cfg{config}
{
  if (cfg.useDebugConfig)
    loadDebugConfig();
}

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  auto& ctx = etna::get_context();

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

  defaultSampler = etna::Sampler(etna::Sampler::CreateInfo{.name = "default_sampler"});

  constants.emplace(wc, [&ctx](size_t) {
    return ctx.createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(constantsData),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
      .name = "constants"});
  });
  lights.emplace(wc, [&ctx](size_t) {
    return ctx.createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(sceneMgr->getLights()),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
      .name = "lights"});
  });
  constants->iterate([](auto& buf) { buf.map(); });
  lights->iterate([](auto& buf) { buf.map(); });

  histData = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = sizeof(HistogramData),
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "hist_data"});
  jndBinsData = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = hdrImagePixelCount() / 8,
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "jnd_bins_buffer"});
}

void WorldRenderer::loadScene(std::filesystem::path path)
{
  // @TODO: make recallable, i.e. implement cleanup

  auto& ctx = etna::get_context();

  sceneMgr->selectScene(path, cfg.testMultiplexScene ? cfg.testMultiplexing : SceneMultiplexing{});

  if (sceneMgr->hasTerrain())
  {
    spdlog::info("JB_terrain: terrain loaded!");

    terrain.emplace(TerrainRenderingData{});

    memcpy(&terrain->sourceData, &sceneMgr->getTerrainData(), sizeof(sceneMgr->getTerrainData()));
    terrain->source = ctx.createBuffer(etna::Buffer::CreateInfo{
      .size = sizeof(sceneMgr->getTerrainData()),
      .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
      .name = "terrain"});

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

    constantsData.toroidalUpdatePlayerWorldPos = {-FLT_MAX, -FLT_MAX, -FLT_MAX};
  }
  else
    spdlog::info("JB_terrain: terrain not present");

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
    texBindings.emplace_back(
      etna::Binding{0, tex.genBinding({}, vk::ImageLayout::eShaderReadOnlyOptimal), uint32_t(i)});
    registerManagedImage(
      tex,
      std::string{"bindless_tex_"} + std::to_string(i) + "[" + std::string{tex.getName()} + "]");
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

  culledInstancesBuf = ctx.createBuffer(etna::Buffer::CreateInfo{
    .size = sceneMgr->getInstances().size() * sizeof(DrawableInstance),
    .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
    .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
    .name = "culledInstancesBuf",
  });
}

void WorldRenderer::loadShaders()
{
  etna::create_program(
    "static_mesh",
    {RENDERER_SHADERS_ROOT "static_mesh.frag.spv", RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
  etna::create_program(
    "terrain_mesh",
    {RENDERER_SHADERS_ROOT "terrain_mesh.frag.spv",
     RENDERER_SHADERS_ROOT "terrain_mesh.vert.spv",
     RENDERER_SHADERS_ROOT "terrain_mesh.tesc.spv",
     RENDERER_SHADERS_ROOT "terrain_mesh.tese.spv"});
  etna::create_program("culling", {RENDERER_SHADERS_ROOT "culling.comp.spv"});
  etna::create_program(
    "reset_indirect_commands", {RENDERER_SHADERS_ROOT "reset_indirect_commands.comp.spv"});
  etna::create_program("clipmap_gen", {RENDERER_SHADERS_ROOT "clipmap_gen.comp.spv"});
  etna::create_program("histogram_clear", {RENDERER_SHADERS_ROOT "histogram_clear.comp.spv"});
  etna::create_program("histogram_minmax", {RENDERER_SHADERS_ROOT "histogram_minmax.comp.spv"});
  etna::create_program("histogram_binning", {RENDERER_SHADERS_ROOT "histogram_binning.comp.spv"});
  etna::create_program(
    "histogram_pre_refine", {RENDERER_SHADERS_ROOT "histogram_pre_refine.comp.spv"});
  etna::create_program(
    "histogram_distribution", {RENDERER_SHADERS_ROOT "histogram_distribution.comp.spv"});
  etna::create_program(
    "histogram_debug",
    {RENDERER_SHADERS_ROOT "histogram_debug.frag.spv", QuadRenderer::VERTEX_SHADER_PATH});
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

  staticMeshPipeline = MeshPipeline{pipelineManager, "static_mesh", meshPipelineCreateInfo};
  terrainMeshPipeline = MeshPipeline{pipelineManager, "terrain_mesh", terrainPipelineCreateInfo};

  cullingPipeline = pipelineManager.createComputePipeline("culling", {});
  resetIndirectCommandsPipeline =
    pipelineManager.createComputePipeline("reset_indirect_commands", {});

  generateClipmapPipeline = pipelineManager.createComputePipeline("clipmap_gen", {});

  gbufferResolver = std::make_unique<PostfxRenderer>(PostfxRenderer::CreateInfo{
    "gbuffer_resolve",
    RENDERER_SHADERS_ROOT "gbuffer_resolve.frag.spv",
    vk::Format::eR32G32B32A32Sfloat,
    {resolution.x, resolution.y}});

  tonemapper = std::make_unique<PostfxRenderer>(PostfxRenderer::CreateInfo{
    "tonemap",
    RENDERER_SHADERS_ROOT "tonemap.frag.spv",
    swapchain_format,
    {resolution.x, resolution.y}});

  bboxRenderer = std::make_unique<BboxRenderer>(
    BboxRenderer::CreateInfo{swapchain_format, {resolution.x, resolution.y}});
  quadRenderer = std::make_unique<QuadRenderer>(QuadRenderer::CreateInfo{swapchain_format});

  clearHistPipeline = pipelineManager.createComputePipeline("histogram_clear", {});
  calculateHistMinmaxPipeline = pipelineManager.createComputePipeline("histogram_minmax", {});
  calculateHistDensityPipeline = pipelineManager.createComputePipeline("histogram_binning", {});
  calculatePreRefinedHistPipeline =
    pipelineManager.createComputePipeline("histogram_pre_refine", {});
  calculateHistDistributionPipeline =
    pipelineManager.createComputePipeline("histogram_distribution", {});

  histogramDebugPipeline = pipelineManager.createGraphicsPipeline(
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

  debugDrawers.emplace(
    "histrogram_debug_view",
    DebugDrawer{
      [this](vk::CommandBuffer cb, vk::Image ti, vk::ImageView tiv) {
        auto programInfo = etna::get_shader_program("histogram_debug");
        auto set = etna::create_descriptor_set(
          programInfo.getDescriptorLayoutId(0), cb, {etna::Binding{0, histData.genBinding()}});

        etna::RenderTargetState renderTargets{
          cb,
          {{0, 0}, {resolution.x / 2, resolution.y / 2}},
          {{.image = ti, .view = tiv, .loadOp = vk::AttachmentLoadOp::eLoad}},
          {}};

        cb.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics,
          histogramDebugPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});
        cb.bindPipeline(vk::PipelineBindPoint::eGraphics, histogramDebugPipeline.getVkPipeline());

        cb.draw(3, 1, 0, 0);
      },
      [] {}});
}

void WorldRenderer::debugInput(const Keyboard&, const Mouse&, bool mouse_captured)
{
  settingsGuiEnabled = !mouse_captured;
}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;

  const float aspect = float(resolution.x) / float(resolution.y);

  // calc camera matrix
  {
    const auto proj = packet.mainCam.projTm(aspect);
    constantsData.mView = packet.mainCam.viewTm();
    constantsData.mProjView = proj * constantsData.mView;
  }

  // pass frustum dimensions
  {
    constantsData.viewFrustum.nearY =
      glm::tan(glm::radians(packet.mainCam.fov * 0.5f)) * packet.mainCam.zNear;
    constantsData.viewFrustum.nearX = aspect * constantsData.viewFrustum.nearY;
    constantsData.viewFrustum.nearZ = packet.mainCam.zNear;
    constantsData.viewFrustum.farZ = packet.mainCam.zFar;
  }

  {
    dt = prevTime >= 0.f ? (packet.currentTime - prevTime) : 0.f;
    prevTime = packet.currentTime;
  }

  {
    constantsData.playerWorldPos = packet.mainCam.position;
    if (terrain)
    {
      constantsData.toroidalOffset =
        constantsData.playerWorldPos - constantsData.toroidalUpdatePlayerWorldPos;
      const float xzDisp =
        glm::length(glm::vec2{constantsData.toroidalOffset.x, constantsData.toroidalOffset.z});
      if (xzDisp >= CLIPMAP_UPDATE_MIN_DPOS)
      {
        terrain->needToroidalUpdate = true;
      }
    }
  }

  {
    constantsData.cullingMode = doSatCulling ? CullingMode::SAT : CullingMode::PER_VERTEX;
    constantsData.useTonemapping = doTonemapping;
    constantsData.useSharedMemForTonemapping = useSharedMemForTonemapping;
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

  // @NOTE done here to make it to the gpu frame. A bit hacky.
  bool updateClipmap = false;
  if (terrain && terrain->needToroidalUpdate)
  {
    updateClipmap = true;
    terrain->needToroidalUpdate = false;
    constantsData.toroidalUpdatePlayerWorldPos = constantsData.playerWorldPos;
  }

  memcpy(constants->get().data(), &constantsData, sizeof(constantsData));
  memcpy(lights->get().data(), &sceneMgr->getLights(), sizeof(sceneMgr->getLights()));

  // @TODO pack culling & draw back into a renderScene method (will need for shadow maps)

  {
    ETNA_PROFILE_GPU(cmd_buf, renderDeferred);

    if (updateClipmap)
    {
      ETNA_PROFILE_GPU(cmd_buf, generateClipmap);

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

      const auto xzOffset =
        glm::vec2{constantsData.toroidalOffset.x, constantsData.toroidalOffset.z};
      for (size_t i = 0; i < CLIPMAP_LEVEL_COUNT; ++i)
      {
        const auto dims = calculate_toroidal_dims(xzOffset, shader_uint(i));
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

    emitBarriers(
      cmd_buf,
      {vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
        .srcAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .buffer = sceneMgr->getIndirectCommandsBuf().get(),
        .size = sceneMgr->getIndirectCommands().size_bytes()}});

    {
      ETNA_PROFILE_GPU(cmd_buf, reset);
      auto programInfo = etna::get_shader_program("reset_indirect_commands");
      auto set = etna::create_descriptor_set(
        programInfo.getDescriptorLayoutId(0),
        cmd_buf,
        {etna::Binding{0, sceneMgr->getIndirectCommandsBuf().genBinding()}});
      cmd_buf.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        resetIndirectCommandsPipeline.getVkPipelineLayout(),
        0,
        {set.getVkSet()},
        {});
      cmd_buf.bindPipeline(
        vk::PipelineBindPoint::eCompute, resetIndirectCommandsPipeline.getVkPipeline());

      cmd_buf.dispatch(
        get_linear_wg_count(uint32_t(sceneMgr->getIndirectCommands().size()), BASE_WORK_GROUP_SIZE),
        1,
        1);
    }

    emitBarriers(
      cmd_buf,
      {vk::BufferMemoryBarrier2{
         .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
         .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .dstAccessMask =
           vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
         .buffer = sceneMgr->getIndirectCommandsBuf().get(),
         .size = sceneMgr->getIndirectCommands().size_bytes()},
       vk::BufferMemoryBarrier2{
         .srcStageMask = vk::PipelineStageFlagBits2::eVertexShader,
         .srcAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
         .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
         .buffer = culledInstancesBuf.get(),
         .size = sceneMgr->getInstances().size() * sizeof(DrawableInstance)}});

    {
      ETNA_PROFILE_GPU(cmd_buf, culling);

      auto programInfo = etna::get_shader_program("culling");
      auto set = etna::create_descriptor_set(
        programInfo.getDescriptorLayoutId(0),
        cmd_buf,
        {etna::Binding{0, sceneMgr->getInstanceMatricesBuf().genBinding()},
         etna::Binding{1, sceneMgr->getInstancesBuf().genBinding()},
         etna::Binding{2, sceneMgr->getBboxesBuf().genBinding()},
         etna::Binding{3, culledInstancesBuf.genBinding()},
         etna::Binding{4, sceneMgr->getIndirectCommandsBuf().genBinding()},
         etna::Binding{8, constants->get().genBinding()}});

      cmd_buf.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        cullingPipeline.getVkPipelineLayout(),
        0,
        {set.getVkSet()},
        {});
      cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, cullingPipeline.getVkPipeline());

      cmd_buf.dispatch(
        get_linear_wg_count(uint32_t(sceneMgr->getInstances().size()), BASE_WORK_GROUP_SIZE), 1, 1);
    }

    emitBarriers(
      cmd_buf,
      {vk::BufferMemoryBarrier2{
         .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .srcAccessMask =
           vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
         .dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
         .dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
         .buffer = sceneMgr->getIndirectCommandsBuf().get(),
         .size = sceneMgr->getIndirectCommands().size_bytes()},
       vk::BufferMemoryBarrier2{
         .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
         .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader,
         .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
         .buffer = culledInstancesBuf.get(),
         .size = sceneMgr->getInstances().size() * sizeof(DrawableInstance)}});

    {
      ETNA_PROFILE_GPU(cmd_buf, deferredGpass);

      etna::RenderTargetState renderTargets{
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = gbufAlbedo.get(), .view = gbufAlbedo.getView({})},
         {.image = gbufMaterial.get(), .view = gbufMaterial.getView({})},
         {.image = gbufNormal.get(), .view = gbufNormal.getView({})}},
        {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})}};

      if (drawScene)
      {
        ETNA_PROFILE_GPU(cmd_buf, sceneMeshes);

        auto programInfo = etna::get_shader_program("static_mesh");
        const auto& pipe = staticMeshPipeline.get(wireframe);

        auto set = etna::create_descriptor_set(
          programInfo.getDescriptorLayoutId(0),
          cmd_buf,
          {etna::Binding{0, sceneMgr->getInstanceMatricesBuf().genBinding()},
           etna::Binding{1, culledInstancesBuf.genBinding()},
           etna::Binding{8, constants->get().genBinding()}});
        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics,
          pipe.getVkPipelineLayout(),
          0,
          {set.getVkSet(),
           materialParamsDsetFrag.getVkSet(),
           bindlessTexturesDsetFrag.getVkSet(),
           bindlessSamplersDsetFrag.getVkSet()},
          {});

        cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipe.getVkPipeline());

        cmd_buf.bindVertexBuffers(0, {sceneMgr->getVertexBuffer()}, {0});
        cmd_buf.bindIndexBuffer(sceneMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

        auto [offset, count] = sceneMgr->getSceneObjectsIndirectCommandsSubrange();

        cmd_buf.drawIndexedIndirect(
          sceneMgr->getIndirectCommandsBuf().get(), offset, count, sizeof(IndirectCommand));
      }

      if (terrain && drawTerrain)
      {
        ETNA_PROFILE_GPU(cmd_buf, terrain);

        auto programInfo = etna::get_shader_program("terrain_mesh");
        const auto& pipe = terrainMeshPipeline.get(wireframe);

        std::vector<etna::Binding> bindings{};
        bindings.reserve(
          terrain->geometryLevelsSamplerBindings.size() +
          terrain->normalLevelsSamplerBindings.size() +
          terrain->albedoLevelsSamplerBindings.size() +
          terrain->matdataLevelsSamplerBindings.size() + 4);
        bindings.emplace_back(0, sceneMgr->getBboxesBuf().genBinding());
        bindings.emplace_back(1, culledInstancesBuf.genBinding());
        for (const auto& b : terrain->geometryLevelsSamplerBindings)
          bindings.push_back(b);
        for (const auto& b : terrain->normalLevelsSamplerBindings)
          bindings.push_back(b);
        for (const auto& b : terrain->albedoLevelsSamplerBindings)
          bindings.push_back(b);
        for (const auto& b : terrain->matdataLevelsSamplerBindings)
          bindings.push_back(b);
        bindings.emplace_back(7, terrain->source.genBinding());
        bindings.emplace_back(8, constants->get().genBinding());

        auto set =
          etna::create_descriptor_set(programInfo.getDescriptorLayoutId(0), cmd_buf, bindings);

        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics, pipe.getVkPipelineLayout(), 0, {set.getVkSet()}, {});

        cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, pipe.getVkPipeline());

        auto [offset, count] = sceneMgr->getTerrainIndirectCommandsSubrange();

        cmd_buf.pushConstants<shader_uint>(
          pipe.getVkPipelineLayout(),
          vk::ShaderStageFlagBits::eVertex,
          0,
          shader_uint(sceneMgr->getIndirectCommands()[offset].firstInstance));

        cmd_buf.drawIndexedIndirect(
          sceneMgr->getIndirectCommandsBuf().get(),
          offset * sizeof(IndirectCommand),
          count,
          sizeof(IndirectCommand));
      }
    }

    // @TODO: should resolve be in renderer rather than in world renderer?
    {
      ETNA_PROFILE_GPU(cmd_buf, deferredResolve);

      auto set = etna::create_descriptor_set(
        gbufferResolver->shaderProgramInfo().getDescriptorLayoutId(0),
        cmd_buf,
        {etna::Binding{1, lights->get().genBinding()},
         etna::Binding{8, constants->get().genBinding()}});

      auto gbufSet = etna::create_descriptor_set(
        gbufferResolver->shaderProgramInfo().getDescriptorLayoutId(1),
        cmd_buf,
        {etna::Binding{
           0, gbufAlbedo.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
         etna::Binding{
           1,
           gbufMaterial.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
         etna::Binding{
           2, gbufNormal.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
         etna::Binding{
           8,
           mainViewDepth.genBinding(
             defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)}});

      cmd_buf.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        gbufferResolver->pipelineLayout(),
        0,
        {set.getVkSet(), gbufSet.getVkSet()},
        {});

      gbufferResolver->render(cmd_buf, hdrTarget.get(), hdrTarget.getView({}));
    }

    {
      ETNA_PROFILE_GPU(cmd_buf, tonemapping);

      emitBarriers(
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
           .size = hdrImagePixelCount() / 8}});

      {
        ETNA_PROFILE_GPU(cmd_buf, histogram_clear);

        auto programInfo = etna::get_shader_program("histogram_clear");
        auto set = etna::create_descriptor_set(
          programInfo.getDescriptorLayoutId(0),
          cmd_buf,
          {etna::Binding{0, histData.genBinding()}, etna::Binding{1, jndBinsData.genBinding()}});

        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          clearHistPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});
        cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, clearHistPipeline.getVkPipeline());

        // @TODO: many elems per thread?
        cmd_buf.dispatch(
          get_linear_wg_count(
            hdrImagePixelCount() / (sizeof(shader_uint) * 8), HISTOGRAM_WORK_GROUP_SIZE),
          1,
          1);
      }

      emitBarriers(
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
        hdrTarget.get(),
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
             0,
             hdrTarget.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
           etna::Binding{1, histData.genBinding()},
           etna::Binding{8, constants->get().genBinding()}});

        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          calculateHistMinmaxPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});
        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute, calculateHistMinmaxPipeline.getVkPipeline());

        cmd_buf.dispatch(
          get_linear_wg_count(
            hdrImagePixelCount(), HISTOGRAM_WORK_GROUP_SIZE * HISTOGRAM_PIXELS_PER_THREAD),
          1,
          1);
      }

      emitBarriers(
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
          calculatePreRefinedHistPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});
        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute, calculatePreRefinedHistPipeline.getVkPipeline());

        cmd_buf.dispatch(1, 1, 1);
      }

      emitBarriers(
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
           .size = hdrImagePixelCount() / 8}});

      {
        ETNA_PROFILE_GPU(cmd_buf, histogram_binning);

        auto programInfo = etna::get_shader_program("histogram_binning");
        auto set = etna::create_descriptor_set(
          programInfo.getDescriptorLayoutId(0),
          cmd_buf,
          {etna::Binding{
             0,
             hdrTarget.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
           etna::Binding{1, histData.genBinding()},
           etna::Binding{2, jndBinsData.genBinding()}});

        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          calculateHistDensityPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});
        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute, calculateHistDensityPipeline.getVkPipeline());

        cmd_buf.dispatch(
          get_linear_wg_count(
            hdrImagePixelCount(), HISTOGRAM_WORK_GROUP_SIZE * HISTOGRAM_PIXELS_PER_THREAD),
          1,
          1);
      }

      emitBarriers(
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
           .size = hdrImagePixelCount() / 8}});

      {
        ETNA_PROFILE_GPU(cmd_buf, histogram_distribution);

        auto programInfo = etna::get_shader_program("histogram_distribution");
        auto set = etna::create_descriptor_set(
          programInfo.getDescriptorLayoutId(0), cmd_buf, {etna::Binding{0, histData.genBinding()}});

        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eCompute,
          calculateHistDistributionPipeline.getVkPipelineLayout(),
          0,
          {set.getVkSet()},
          {});
        cmd_buf.bindPipeline(
          vk::PipelineBindPoint::eCompute, calculateHistDistributionPipeline.getVkPipeline());

        cmd_buf.dispatch(1, 1, 1);
      }

      emitBarriers(
        cmd_buf,
        {vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .srcAccessMask =
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
          .dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader,
          .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
          .buffer = histData.get(),
          .size = sizeof(HistogramData)}});

      {
        ETNA_PROFILE_GPU(cmd_buf, tonemapping_apply);

        auto set = etna::create_descriptor_set(
          tonemapper->shaderProgramInfo().getDescriptorLayoutId(0),
          cmd_buf,
          {etna::Binding{
             0,
             hdrTarget.genBinding(defaultSampler.get(), vk::ImageLayout::eShaderReadOnlyOptimal)},
           etna::Binding{1, histData.genBinding()},
           etna::Binding{8, constants->get().genBinding()}});

        cmd_buf.bindDescriptorSets(
          vk::PipelineBindPoint::eGraphics, tonemapper->pipelineLayout(), 0, {set.getVkSet()}, {});

        tonemapper->render(cmd_buf, target_image, target_image_view);
      }
    }

    {
      ETNA_PROFILE_GPU(cmd_buf, debugDrawing);

      if (drawBboxes)
      {
        bboxRenderer->render(
          cmd_buf,
          target_image,
          target_image_view,
          sceneMgr->getInstanceMatricesBuf(),
          sceneMgr->getInstancesBuf(),
          sceneMgr->getBboxesBuf(),
          constants->get(),
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
    if (terrain)
    {
      ImGui::Text(
        "Last toroidal update pos: [%.3f, %.3f, %.3f]",
        constantsData.toroidalUpdatePlayerWorldPos.x,
        constantsData.toroidalUpdatePlayerWorldPos.y,
        constantsData.toroidalUpdatePlayerWorldPos.z);
      ImGui::Text(
        "Toroidal offset: [%.3f, %.3f, %.3f]",
        constantsData.toroidalOffset.x,
        constantsData.toroidalOffset.y,
        constantsData.toroidalOffset.z);
    }
    ImGui::End();
  }

  if (settingsGuiEnabled)
  {
    {
      ImGui::Begin("Lights");

      const bool prevDirVal = directionalLightsAreOn;
      const bool prevPointVal = pointLightsAreOn;
      const bool prevSpotVal = spotLightsAreOn;

      ImGui::Checkbox("Enable directional lights", &directionalLightsAreOn);
      ImGui::Checkbox("Enable point lights", &pointLightsAreOn);
      ImGui::Checkbox("Enable spot lights", &spotLightsAreOn);

      if (directionalLightsAreOn != prevDirVal)
        setAllDirLightsIntensity(directionalLightsAreOn ? 1.f : 0.f);
      if (pointLightsAreOn != prevPointVal)
        setAllPointLightsIntensity(pointLightsAreOn ? 1.f : 0.f);
      if (spotLightsAreOn != prevSpotVal)
        setAllSpotLightsIntensity(spotLightsAreOn ? 1.f : 0.f);

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
      ImGui::Checkbox("Use SAT culling", &doSatCulling);
      ImGui::Checkbox("Use tonemapping", &doTonemapping);
      if (doTonemapping)
        ImGui::Checkbox("Use shared memory for tonemapping", &useSharedMemForTonemapping);
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
  dst = etna::get_context().createImage(std::move(ci));
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
          glm::clamp(currentDebugTexMip, 0u, uint32_t(img.getMipLevelCount() - 1));
        currentDebugTexLayer =
          glm::clamp(currentDebugTexLayer, 0u, uint32_t(img.getLayerCount() - 1));
      }});
}

void WorldRenderer::emitBarriers(
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
  doSatCulling = unwrap(reader.read<bool>());
  doTonemapping = unwrap(reader.read<bool>());
  useSharedMemForTonemapping = unwrap(reader.read<bool>());

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
  ETNA_VERIFY(writer.write(doSatCulling));
  ETNA_VERIFY(writer.write(doTonemapping));
  ETNA_VERIFY(writer.write(useSharedMemForTonemapping));

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
