#define _USE_MATH_DEFINES

#include "WorldRenderer.hpp"
#include "render_utils/PostfxRenderer.hpp"

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

  // @TODO: more graceful
  ETNA_ASSERT(sceneMgr->getTextures().size() <= MAX_BINDLESS_TEXTURES);
  ETNA_ASSERT(sceneMgr->getSamplers().size() <= MAX_BINDLESS_SAMPLERS);

  // @TODO: pull out
  auto& ctx = etna::get_context();

  static constexpr std::array<vk::DescriptorPoolSize, 2> BINDLESS_POOL_SIZES{
    vk::DescriptorPoolSize{vk::DescriptorType::eSampler, MAX_BINDLESS_SAMPLERS},
    vk::DescriptorPoolSize{vk::DescriptorType::eSampledImage, MAX_BINDLESS_TEXTURES}};

  vk::DescriptorPoolCreateInfo pinfo{
    .maxSets = 1,
    .poolSizeCount = uint32_t(BINDLESS_POOL_SIZES.size()),
    .pPoolSizes = BINDLESS_POOL_SIZES.data()};
  bindlessDescriptorPool =
    etna::unwrap_vk_result(ctx.getDevice().createDescriptorPoolUnique(pinfo));

  auto layoutId = etna::get_shader_program("static_mesh").getDescriptorLayoutId(0);
  auto setLayouts = {ctx.getDescriptorSetLayouts().getVkLayout(layoutId)};

  vk::DescriptorSetAllocateInfo dainfo{};
  dainfo.setDescriptorPool(bindlessDescriptorPool.get());
  dainfo.setSetLayouts(setLayouts);

  ETNA_VERIFY(
    ctx.getDevice().allocateDescriptorSets(&dainfo, &bindlessDset) == vk::Result::eSuccess);

  std::vector<vk::DescriptorImageInfo> textureInfos, samplerInfos;
  textureInfos.reserve(sceneMgr->getTextures().size());
  samplerInfos.reserve(sceneMgr->getSamplers().size());

  for (const auto& tex : sceneMgr->getTextures())
  {
    textureInfos.emplace_back(vk::DescriptorImageInfo{
      .imageView = tex.getView({}), .imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal});
  }
  for (const auto& smp : sceneMgr->getSamplers())
    samplerInfos.emplace_back(vk::DescriptorImageInfo{.sampler = smp.get()});

  vk::DescriptorBufferInfo materialBufferInfo{
    .buffer = sceneMgr->getMaterialParamsBuf().get(),
    .offset = 0u,
    .range = sceneMgr->getMaterialParamsBufSizeBytes()};

  ctx.getDevice().updateDescriptorSets(
    {
      vk::WriteDescriptorSet{
        .dstSet = bindlessDset,
        .dstBinding = 0,
        .descriptorCount = uint32_t(textureInfos.size()),
        .descriptorType = vk::DescriptorType::eSampledImage,
        .pImageInfo = textureInfos.data()},
      vk::WriteDescriptorSet{
        .dstSet = bindlessDset,
        .dstBinding = 1,
        .descriptorCount = uint32_t(samplerInfos.size()),
        .descriptorType = vk::DescriptorType::eSampler,
        .pImageInfo = samplerInfos.data()},
      vk::WriteDescriptorSet{
        .dstSet = bindlessDset,
        .dstBinding = 2,
        .descriptorCount = 1,
        .descriptorType = vk::DescriptorType::eStorageBuffer,
        .pBufferInfo = &materialBufferInfo},
    },
    {});
}

void WorldRenderer::loadShaders()
{
  etna::create_program(
    "static_mesh",
    {BINDLESS_RENDERER_SHADERS_ROOT "static_mesh.frag.spv",
     BINDLESS_RENDERER_SHADERS_ROOT "static_mesh.vert.spv"});
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

  gbufferResolver = std::make_unique<PostfxRenderer>(PostfxRenderer::CreateInfo{
    "gbuffer_resolve",
    BINDLESS_RENDERER_SHADERS_ROOT "gbuffer_resolve.frag.spv",
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
    worldViewProj = packet.mainCam.projTm(aspect) * packet.mainCam.viewTm();
  }
}

void WorldRenderer::renderScene(
  vk::CommandBuffer cmd_buf, const glm::mat4x4& glob_tm, vk::PipelineLayout pipeline_layout)
{
  if (!sceneMgr->getVertexBuffer())
    return;

  cmd_buf.bindVertexBuffers(0, {sceneMgr->getVertexBuffer()}, {0});
  cmd_buf.bindIndexBuffer(sceneMgr->getIndexBuffer(), 0, vk::IndexType::eUint32);

  pushConst.projView = glob_tm;

  auto instanceMeshes = sceneMgr->getInstanceMeshes();
  auto instanceMatrices = sceneMgr->getInstanceMatrices();

  auto meshes = sceneMgr->getMeshes();
  auto relems = sceneMgr->getRenderElements();

  for (size_t instIdx = 0; instIdx < instanceMeshes.size(); ++instIdx)
  {
    pushConst.modelAndMatId = instanceMatrices[instIdx];
    ETNA_ASSERT(
      pushConst.modelAndMatId[0].w == 0.f && pushConst.modelAndMatId[1].w == 0.f &&
      pushConst.modelAndMatId[2].w == 0.f && pushConst.modelAndMatId[3].w == 1.f);

    const auto meshIdx = instanceMeshes[instIdx];
    if (meshIdx == (uint32_t)(-1))
      continue;

    for (size_t j = 0; j < meshes[meshIdx].relemCount; ++j)
    {
      const auto relemIdx = meshes[meshIdx].firstRelem + j;
      const auto& relem = relems[relemIdx];

      // @TODO: coalesce somehow, or better move projView to ubuf? or sort
      pushConst.modelAndMatId[3].w = std::bit_cast<float>(relem.materialId);
      cmd_buf.pushConstants<PushConstants>(
        pipeline_layout,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
        0,
        {pushConst});

      cmd_buf.drawIndexed(relem.indexCount, 1, relem.indexOffset, relem.vertexOffset, 0);
    }
  }
}

void WorldRenderer::renderWorld(
  vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)
{
  ETNA_PROFILE_GPU(cmd_buf, renderWorld);

  memcpy(constants->get().data(), &constantsData, sizeof(constantsData));
  memcpy(lights->get().data(), &sceneMgr->getLights(), sizeof(sceneMgr->getLights()));

  // draw final scene to screen
  {
    ETNA_PROFILE_GPU(cmd_buf, renderDeferred);

    {
      ETNA_PROFILE_GPU(cmd_buf, deferredGpass);

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
        {bindlessDset},
        {});

      cmd_buf.bindPipeline(vk::PipelineBindPoint::eGraphics, staticMeshPipeline.getVkPipeline());
      renderScene(cmd_buf, worldViewProj, staticMeshPipeline.getVkPipelineLayout());
    }

    // @TODO: etna should not require this for no READ_AFTER_WRITE hazards. Investigate!
    etna::set_state(
      cmd_buf,
      gbufAlbedo.get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);
    etna::set_state(
      cmd_buf,
      gbufMaterial.get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);
    etna::set_state(
      cmd_buf,
      gbufNormal.get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eColor);
    etna::set_state(
      cmd_buf,
      mainViewDepth.get(),
      vk::PipelineStageFlagBits2::eFragmentShader,
      vk::AccessFlagBits2::eShaderSampledRead,
      vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageAspectFlagBits::eDepth);

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

      cmd_buf.pushConstants<glm::mat4x4>(
        gbufferResolver->pipelineLayout(), vk::ShaderStageFlagBits::eFragment, 0, {worldViewProj});
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
  if (settingsGuiEnabled)
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
