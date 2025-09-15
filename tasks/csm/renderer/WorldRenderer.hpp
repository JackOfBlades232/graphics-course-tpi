#pragma once

#include "FramePacket.hpp"
#include "Config.hpp"

#include <render_components/IComponent.hpp>
#include <render_components/ITonemapper.hpp>
#include <render_components/DebugDrawer.hpp>

#include <render_utils/PostfxRenderer.hpp>
#include <render_utils/BitonicSort.hpp>
#include <render_utils/BboxRenderer.hpp>
#include <render_utils/QuadRenderer.hpp>

#include <scene/ViewContext.hpp>
#include <scene/SceneManager.hpp>

#include <wsi/Keyboard.hpp>
#include <wsi/Mouse.hpp>

#include <constants.h>
#include <terrain.h>
#include <skybox.h>

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/RenderTargetStates.hpp>
#include <glm/glm.hpp>

#include <unordered_map>
#include <initializer_list>
#include <concepts>


class WorldRenderer
{
public:
  WorldRenderer(const etna::GpuWorkCount& wc, const Config& config);

  void loadScene(std::filesystem::path path);

  void loadShaders();
  void allocateResources(glm::uvec2 swapchain_resolution);
  void setupPipelines(vk::Format swapchain_format);

  void debugInput(const Keyboard& kb, const Mouse& ms, bool mouse_captured);
  void update(const FramePacket& packet);
  void drawGui();
  void renderWorld(
    vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view);

private:
  enum class SceneRenderingPass
  {
    COLOR,
    WIRE_COLOR,
    SHADOW,
    SHADOW_FRONT_CULLED,

    COUNT
  };

  static constexpr size_t SCENE_RPASS_COUNT = size_t(SceneRenderingPass::COUNT);

  struct SceneRenderPassInfo
  {
    SceneRenderingPass pass;
    ViewContext* vctx;
    ViewParams vparams;
    etna::RenderTargetState::RenderPassInfo rtargetInfo;
    bool depthBias = false;
    float depthBiasConstantFactor = 0.f;
    float depthBiasClamp = 0.f;
    float depthBiasSlopeFactor = 0.f;
  };

  struct MeshPipeline
  {
    etna::GraphicsPipeline pipelines[SCENE_RPASS_COUNT];
    std::optional<etna::ShaderProgramInfo> programs[SCENE_RPASS_COUNT];

    MeshPipeline(
      etna::PipelineManager& pipeman,
      const char* prog_name,
      const char* vertex_prog_name,
      const etna::GraphicsPipeline::CreateInfo& ci);

    MeshPipeline() = default;

    const etna::GraphicsPipeline& get(SceneRenderingPass pass) const
    {
      return pipelines[size_t(pass)];
    }
    const etna::ShaderProgramInfo& getProg(SceneRenderingPass pass) const
    {
      return *programs[size_t(pass)];
    }
  };

  struct TerrainRenderingData
  {
    etna::Image geometryClipmap{};
    etna::Image normalClipmap{};
    etna::Image albedoClipmap{};
    etna::Image matdataClipmap{};
    std::vector<etna::Binding> geometryLevelsBindings{};
    std::vector<etna::Binding> normalLevelsBindings{};
    std::vector<etna::Binding> albedoLevelsBindings{};
    std::vector<etna::Binding> matdataLevelsBindings{};
    std::vector<etna::Binding> geometryLevelsSamplerBindings{};
    std::vector<etna::Binding> normalLevelsSamplerBindings{};
    std::vector<etna::Binding> albedoLevelsSamplerBindings{};
    std::vector<etna::Binding> matdataLevelsSamplerBindings{};

    etna::Image perCellMinHeightLut{};
    etna::Image perCellMaxHeightLut{};
    etna::Sampler perCellRangeLutSampler{};

    etna::Buffer source{};
    TerrainSourceData sourceData{};

    etna::Sampler clipmapSampler{};

    bool needToroidalUpdate = false;
    bool needHmapRangeUpdate = false;
    bool invalidateClipmapRequested = false;
    bool invalidateHmapRangeRequested = false;
  };

  struct SkyboxRenderingData
  {
    etna::Buffer source{};
    SkyboxSourceData sourceData{};
  };

  enum class TonemappingTechnique
  {
    HISTOGRAM_EQ = 0,
    REINHARD,
    ACES,

    COUNT
  };
  static constexpr size_t TONEMAPPING_TECHNIQUE_COUNT = size_t(TonemappingTechnique::COUNT);

  static constexpr std::array<std::string_view, TONEMAPPING_TECHNIQUE_COUNT>
    TONEMAPPING_TECHNIQUE_NAMES = {"Histogram equalization", "Reinhard", "ACES"};

  static constexpr size_t SHADOW_TECHNIQUE_COUNT = size_t(ShadowTechnique::COUNT);

  static constexpr std::array<std::string_view, SHADOW_TECHNIQUE_COUNT> SHADOW_TECHNIQUE_NAMES = {
    "Hard", "PCF"};

  struct ShadowsSettings
  {
    bool enable = true;
    bool depthBias = true;
    bool frontFaceCull = false;
    uint8_t pad1_{};
    ShadowTechnique technique = ShadowTechnique::PCF;
    float depthBiasConstantFactor = 1.25f;
    float depthBiasClamp = 0.f;
    float depthBiasSlopeFactor = 1.75f;

    friend bool operator==(const ShadowsSettings& s1, const ShadowsSettings& s2) = default;
    friend bool operator!=(const ShadowsSettings& s1, const ShadowsSettings& s2) = default;
  };

private:
  std::unique_ptr<SceneManager> sceneMgr;

  ViewContextManager* viewCtxMgr;

  Camera mainCam;
  std::optional<ViewContext> mainViewContext;

  std::unique_ptr<PostfxRenderer> gbufferResolver{};
  std::optional<MeshPipeline> staticMeshPipeline{};
  std::optional<MeshPipeline> terrainMeshPipeline{};
  etna::ComputePipeline generateClipmapPipeline{};
  etna::ComputePipeline prepareTerrainCellLutPipeline{};
  etna::ComputePipeline transferLightMatsPipeline{};

  std::vector<std::unique_ptr<IComponent>> rcomponents{};

  std::array<ITonemapper*, TONEMAPPING_TECHNIQUE_COUNT> tonemapperComps{};

  etna::Image hdrTarget;
  etna::Image gbufAlbedo, gbufMaterial, gbufNormal;
  etna::Image mainViewDepth;

  etna::Buffer lightMatricesBuf;

  std::optional<etna::GpuSharedResource<etna::Buffer>> constants;
  std::optional<etna::GpuSharedResource<etna::Buffer>> lights;

  std::vector<std::array<ViewContext, 6>> pointLightViews{};
  std::vector<ViewContext> spotLightViews{};
  std::vector<std::array<ViewContext, CSM_CASCADE_COUNT>> directionalLightCascadeViews{};

  bool pointLightsSettingsDirty = true;
  bool spotLightsSettingsDirty = true;
  bool directionalLightsSettingsDirty = true;

  UniformLights prevLights{};

  std::optional<TerrainRenderingData> terrain{};
  std::optional<SkyboxRenderingData> skybox{};

  // @TODO: unify with one in scene manager
  etna::Sampler defaultSampler;

  etna::Buffer stubUniBuffer;
  etna::Buffer stubStorageBuffer;

  etna::PersistentDescriptorSet materialParamsDsetFrag, bindlessTexturesDsetFrag,
    bindlessSamplersDsetFrag;
  etna::PersistentDescriptorSet materialParamsDsetComp, bindlessTexturesDsetComp,
    bindlessSamplersDsetComp;
  bool initialTransition = true; // @HACK

  const etna::GpuWorkCount& wc;

  Constants constantsData = {};

  float prevTime = -1.f;
  float dt = 0.f;

  glm::uvec2 resolution;
  const Config& cfg;

  // @DEBUG
  std::unique_ptr<BboxRenderer> bboxRenderer{};
  std::unique_ptr<QuadRenderer> quadRenderer{};

  DebugDrawersRegistry debugDrawers{};
  std::optional<DebugDrawersRegistryKey> currentDebugDrawer{};

  uint32_t currentDebugTexMip = 0;
  uint32_t currentDebugTexLayer = 0;
  glm::vec2 currentDebugTexColorRange = {0.f, 1.f};
  bool currentDebugTexShowR = true;
  bool currentDebugTexShowG = true;
  bool currentDebugTexShowB = true;
  bool currentDebugTexShowA = true;
  bool settingsGuiEnabled = false;
  bool drawBboxes = false;
  bool wireframe = false;
  bool drawScene = true;
  bool drawTerrain = true;
  bool drawTerrainSplattedDetail = true;
  bool doSatCulling = true;
  bool enableSkybox = true;
  bool doTonemapping = true;
  bool useSharedMemForTonemapping = false;
  ShadowsSettings pointLightShadowsSettings{};
  ShadowsSettings spotLightShadowsSettings{};
  ShadowsSettings directionalLightShadowsSettings{};
  bool drawCascadesInSolidColor = false;
  // @TODO: graduate to JB_terrain
  float terrainNoiseRelHeightAmp = 0.001f;
  float terrainNoisePeriod = 0.25f;
  float histEqTonemappingRegW = 0.5f, histEqTonemappingRefinedW = 0.5f;
  // @TODO: find a way to deal with jittering from lum outliers?
  float histEqTonemappingMinAdmissibleLum = 0.0f, histEqTonemappingMaxAdmissibleLum = 10.f;
  float acesExposure = 2.f;
  float csmSplitLambda = 0.5f;
  TonemappingTechnique currentTonemappingTechnique = TonemappingTechnique::ACES;

private:
  void renderScene(vk::CommandBuffer cmd_buf, SceneRenderPassInfo&& srpi);

  void createManagedImage(etna::Image& dst, etna::Image::CreateInfo&& ci);
  void registerManagedImage(
    const etna::Image& img, std::optional<std::string> name_override = std::nullopt);

  void loadDebugConfig();
  void saveDebugConfig();

  void setAllDirLightsIntensity(float val);
  void setAllPointLightsIntensity(float val);
  void setAllSpotLightsIntensity(float val);

  shader_uint hdrImagePixelCount() const
  {
    return hdrTarget.getExtent().width * hdrTarget.getExtent().height;
  }

  float aspect() const { return float(resolution.x) / float(resolution.y); }

  void registerViewContextManager()
  {
    auto mgr = std::make_unique<ViewContextManager>(wc, *sceneMgr);
    viewCtxMgr = mgr.get();
    rcomponents.emplace_back(std::move(mgr));
  }

  template <std::derived_from<ITonemapper> T>
  void registerTonemapper(TonemappingTechnique technique)
  {
    auto tonemapper = std::make_unique<T>();
    tonemapperComps[size_t(technique)] = tonemapper.get();
    rcomponents.emplace_back(std::move(tonemapper));
  }

  void queueClipmapInvalidation()
  {
    if (terrain)
      terrain->invalidateClipmapRequested = true;
  }
  void queueHmapRangesInvalidation()
  {
    if (terrain)
      terrain->invalidateHmapRangeRequested = true;
  }
};
