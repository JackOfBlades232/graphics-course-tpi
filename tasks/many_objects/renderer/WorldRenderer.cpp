#define _USE_MATH_DEFINES

#include "WorldRenderer.hpp"

#include <render_utils/PostfxRenderer.hpp>

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

WorldRenderer::WorldRenderer(const etna::GpuWorkCount& wc)
  : sceneMgr{std::make_unique<SceneManager>()}
  , wc{wc}
{
}

void WorldRenderer::allocateResources(glm::uvec2 swapchain_resolution)
{
  resolution = swapchain_resolution;

  auto& ctx = etna::get_context();

  mainViewDepth = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "main_view_depth",
    .format = vk::Format::eD32Sfloat,
    .imageUsage =
      vk::ImageUsageFlagBits::eDepthStencilAttachment | vk::ImageUsageFlagBits::eSampled});

  // @TODO: compact gbuffer
  gbufAlbedo = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "gbuffer_albedo",
    .format = vk::Format::eR32G32B32A32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});
  gbufMaterial = ctx.createImage(etna::Image::CreateInfo{
    .extent = vk::Extent3D{resolution.x, resolution.y, 1},
    .name = "gbuffer_material",
    .format = vk::Format::eR32G32B32A32Sfloat,
    .imageUsage = vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eSampled});
  gbufNormal = ctx.createImage(etna::Image::CreateInfo{
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
}

void WorldRenderer::loadScene(std::filesystem::path path)
{
  // @TODO: make recallable, i.e. implement cleanup

  sceneMgr->selectScene(path);

  auto programInfo = etna::get_shader_program("static_mesh");
  materialParamsDset = etna::create_persistent_descriptor_set(
    programInfo.getDescriptorLayoutId(1),
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
  }
  for (size_t i = 0; i < sceneMgr->getSamplers().size(); ++i)
  {
    const auto& smp = sceneMgr->getSamplers()[i];
    smpBindings.emplace_back(etna::Binding{0, smp.genBinding(), uint32_t(i)});
  }

  bindlessTexturesDset = etna::create_persistent_descriptor_set(
    programInfo.getDescriptorLayoutId(2), std::move(texBindings));
  bindlessSamplersDset = etna::create_persistent_descriptor_set(
    programInfo.getDescriptorLayoutId(3), std::move(smpBindings));

  culledInstancesBuf = etna::get_context().createBuffer(etna::Buffer::CreateInfo{
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
  etna::create_program("culling", {RENDERER_SHADERS_ROOT "culling.comp.spv"});
  etna::create_program(
    "reset_indirect_commands", {RENDERER_SHADERS_ROOT "reset_indirect_commands.comp.spv"});
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

  staticMeshPipeline =
    pipelineManager.createGraphicsPipeline("static_mesh", meshPipelineCreateInfo);
  cullingPipeline = pipelineManager.createComputePipeline("culling", {});
  resetIndirectCommandsPipeline =
    pipelineManager.createComputePipeline("reset_indirect_commands", {});

  gbufferResolver = std::make_unique<PostfxRenderer>(PostfxRenderer::CreateInfo{
    "gbuffer_resolve",
    RENDERER_SHADERS_ROOT "gbuffer_resolve.frag.spv",
    swapchain_format,
    {resolution.x, resolution.y}});
}

void WorldRenderer::debugInput(const Keyboard&, const Mouse&, bool mouse_captured)
{
  settingsGuiEnabled = !mouse_captured;
}

void WorldRenderer::update(const FramePacket& packet)
{
  ZoneScoped;

  // calc camera matrix
  {
    const float aspect = float(resolution.x) / float(resolution.y);
    const auto proj = packet.mainCam.projTm(aspect);
    worldView = packet.mainCam.viewTm();
    worldViewProj = proj * worldView;
  }

  {
    dt = prevTime >= 0.f ? (packet.currentTime - prevTime) : 0.f;
    prevTime = packet.currentTime;
  }
}

void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  // @TODO: unhack
  if (initialTransition)
  {
    materialParamsDset.processBarriers(cmd_buf);
    bindlessTexturesDset.processBarriers(cmd_buf);
    bindlessSamplersDset.processBarriers(cmd_buf);
    initialTransition = false;
  }

  memcpy(constants->get().data(), &constantsData, sizeof(constantsData));
  memcpy(lights->get().data(), &sceneMgr->getLights(), sizeof(sceneMgr->getLights()));

  // @TODO pack culling & draw back into a renderScene method (will need for shadow maps)
  pushConst.projView = worldViewProj;

  // draw final scene to screen
  {
    ETNA_PROFILE_GPU(cmd_buf, renderDeferred);

    std::array resetAndCulledBarriers = {vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
      .srcAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
      .buffer = sceneMgr->getIndirectCommandsBuf().get(),
      .size = sceneMgr->getIndirectCommands().size_bytes()}};
    vk::DependencyInfo dep{
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(resetAndCulledBarriers.size()),
      .pBufferMemoryBarriers = resetAndCulledBarriers.data()};
    cmd_buf.pipelineBarrier2(dep);

    {
      ETNA_PROFILE_GPU(cmd_buf, reset);
      auto programInfo = etna::get_shader_program("reset_indirect_commands");
      auto set = etna::create_descriptor_set(
        programInfo.getDescriptorLayoutId(0),
        cmd_buf,
        {{etna::Binding{0, sceneMgr->getIndirectCommandsBuf().genBinding()}}});
      cmd_buf.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        resetIndirectCommandsPipeline.getVkPipelineLayout(),
        0,
        {set.getVkSet()},
        {});
      cmd_buf.bindPipeline(
        vk::PipelineBindPoint::eCompute, resetIndirectCommandsPipeline.getVkPipeline());

      cmd_buf.dispatch(
        get_linear_wg_count(sceneMgr->getIndirectCommands().size(), BASE_WORK_GROUP_SIZE), 1, 1);
    }

    std::array resetAndCulledBarriers2 = {
      vk::BufferMemoryBarrier2{
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
        .size = sceneMgr->getInstances().size() * sizeof(DrawableInstance)}};
    vk::DependencyInfo dep2{
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(resetAndCulledBarriers2.size()),
      .pBufferMemoryBarriers = resetAndCulledBarriers2.data()};
    cmd_buf.pipelineBarrier2(dep2);

    {
      ETNA_PROFILE_GPU(cmd_buf, culling);

      auto programInfo = etna::get_shader_program("culling");
      auto set = etna::create_descriptor_set(
        programInfo.getDescriptorLayoutId(0),
        cmd_buf,
        {{etna::Binding{0, sceneMgr->getInstanceMatricesBuf().genBinding()}},
         {etna::Binding{1, sceneMgr->getInstancesBuf().genBinding()}},
         {etna::Binding{2, sceneMgr->getBboxesBuf().genBinding()}},
         {etna::Binding{3, culledInstancesBuf.genBinding()}},
         {etna::Binding{4, sceneMgr->getIndirectCommandsBuf().genBinding()}}});

      cmd_buf.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        cullingPipeline.getVkPipelineLayout(),
        0,
        {set.getVkSet()},
        {});
      cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, cullingPipeline.getVkPipeline());
      cmd_buf.pushConstants<PushConstants>(
        cullingPipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, {pushConst});

      cmd_buf.dispatch(
        get_linear_wg_count(sceneMgr->getInstances().size(), BASE_WORK_GROUP_SIZE), 1, 1);
    }

    std::array resetAndCulledBarriers3 = {
      vk::BufferMemoryBarrier2{
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
        .size = sceneMgr->getInstances().size() * sizeof(DrawableInstance)}};
    vk::DependencyInfo dep3{
      .dependencyFlags = vk::DependencyFlagBits::eByRegion,
      .bufferMemoryBarrierCount = static_cast<uint32_t>(resetAndCulledBarriers3.size()),
      .pBufferMemoryBarriers = resetAndCulledBarriers3.data()};
    cmd_buf.pipelineBarrier2(dep3);

    {
      ETNA_PROFILE_GPU(cmd_buf, deferredGpass);

      auto programInfo = etna::get_shader_program("static_mesh");
      auto set = etna::create_descriptor_set(
        programInfo.getDescriptorLayoutId(0),
        cmd_buf,
        {{etna::Binding{0, sceneMgr->getInstanceMatricesBuf().genBinding()}},
         {etna::Binding{1, culledInstancesBuf.genBinding()}}});

      etna::RenderTargetState renderTargets{
        cmd_buf,
        {{0, 0}, {resolution.x, resolution.y}},
        {{.image = gbufAlbedo.get(), .view = gbufAlbedo.getView({})},
         {.image = gbufMaterial.get(), .view = gbufMaterial.getView({})},
         {.image = gbufNormal.get(), .view = gbufNormal.getView({})}},
        {.image = mainViewDepth.get(), .view = mainViewDepth.getView({})}};

      cmd_buf.bindDescriptorSets(
        vk::PipelineBindPoint::eGraphics,
        staticMeshPipeline.getVkPipelineLayout(),
        0,
        {set.getVkSet(),
         materialParamsDset.getVkSet(),
         bindlessTexturesDset.getVkSet(),
         bindlessSamplersDset.getVkSet()},
        {});

      cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, staticMeshPipeline.getVkPipeline());

      cmd_buf.bindVertexBuffers(0, {sceneMgr->getVertexBuffer()}, {0});
      cmd_buf.bindIndexBuffer(sceneMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

      cmd_buf.pushConstants<PushConstants>(
        staticMeshPipeline.getVkPipelineLayout(),
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        {pushConst});

      cmd_buf.drawIndexedIndirect(
        sceneMgr->getIndirectCommandsBuf().get(),
        0,
        sceneMgr->getIndirectCommands().size(),
        sizeof(IndirectCommand));
    }

    // @TODO: should resolve be in renderer rather than in world renderer?
    {
      ETNA_PROFILE_GPU(cmd_buf, deferredResolve);

      auto set = etna::create_descriptor_set(
        gbufferResolver->shaderProgramInfo().getDescriptorLayoutId(0),
        cmd_buf,
        {etna::Binding{1, lights->get().genBinding()}});

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

      pushConstResolve.projView = worldViewProj;
      pushConstResolve.view = worldView;
      cmd_buf.pushConstants<PushConstantsResolve>(
        gbufferResolver->pipelineLayout(),
        vk::ShaderStageFlagBits::eFragment,
        0,
        {pushConstResolve});

      gbufferResolver->render(cmd_buf, target_image, target_image_view);
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
    ImGui::End();
  }

  if (settingsGuiEnabled)
  {
    {
      ImGui::Begin("Lights");

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
  }
}
