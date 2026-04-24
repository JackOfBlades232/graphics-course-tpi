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

#include <utils/MovingAverage.hpp>

#include <wsi/Keyboard.hpp>
#include <wsi/Mouse.hpp>

#include <constants.h>
#include <terrain.h>
#include <grass.h>
#include <skybox.h>
#include <dispatch.h>
#include <ssao.h>

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/RenderTargetStates.hpp>
#include <glm/glm.hpp>

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
    COLOR_AFTER_PREPASS,
    WIRE_COLOR_AFTER_PREPASS,
    SHADOW,
    SHADOW_FRONT_CULLED,
    DEPTH_PREPASS,
    WIRE_DEPTH_PREPASS,

    COUNT
  };
  static constexpr size_t SCENE_RPASS_COUNT = size_t(SceneRenderingPass::COUNT);

  enum class DepthFlavour
  {
    NORMAL,
    REVERSE,

    COUNT
  };
  static constexpr size_t DEPTH_FLAVOUR_COUNT = size_t(DepthFlavour::COUNT);

  static DepthFlavour getDepthFlavour(bool reverse_z)
  {
    return reverse_z ? DepthFlavour::REVERSE : DepthFlavour::NORMAL;
  }

  enum SceneRenderingPassObjectsFlags : uint32_t
  {
    SRPO_STATIC = 1,
    SRPO_TERRAIN = 1 << 1,
    SRPO_VEGETATION = 1 << 2,

    SRPO_ALL = SRPO_STATIC | SRPO_TERRAIN | SRPO_VEGETATION
  };

  struct SceneRenderPassInfo
  {
    SceneRenderingPass pass;
    uint32_t flags;
    ViewContext* vctx;
    ViewParams vparams;
    etna::RenderTargetState::RenderPassInfo rtargetInfo;
    bool skipCulling = false;
    bool depthBias = false;
    float depthBiasConstantFactor = 0.f;
    float depthBiasClamp = 0.f;
    float depthBiasSlopeFactor = 0.f;
  };

  struct MeshPipeline
  {
    etna::GraphicsPipeline pipelines[SCENE_RPASS_COUNT][DEPTH_FLAVOUR_COUNT];
    std::optional<etna::ShaderProgramInfo> programs[SCENE_RPASS_COUNT];

    MeshPipeline(
      etna::PipelineManager& pipeman,
      const char* prog_name,
      const char* no_prepass_prog_name,
      const char* shadow_prog_name,
      const char* depth_prog_name,
      const etna::GraphicsPipeline::CreateInfo& ci);

    MeshPipeline() = default;

    const etna::GraphicsPipeline& get(SceneRenderingPass pass, bool reverse_z = false) const
    {
      return pipelines[size_t(pass)][size_t(getDepthFlavour(reverse_z))];
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

    etna::Buffer source{};
    TerrainSourceData sourceData{};

    etna::Buffer chunkHeightBoundsBuf{};

    etna::Sampler clipmapSampler{};

    bool needToroidalUpdate = false;
    bool invalidateClipmapRequested = false;
  };

  struct VegetationRenderingData
  {
    etna::Buffer culledChunkBuffer;
    etna::Buffer indirectDispatchBuffer;
    etna::Buffer grassInstancesBuffer;
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
    "Simple",
    "PCF3X3",
    "PCF5X5",
    "PCF7X7",
    "PCF9X9",
  };

  struct ShadowsSettings
  {
    bool enable = true;
    bool depthBias = true;
    bool frontFaceCull = false;
    uint8_t pad1_{};
    ShadowTechnique technique = ShadowTechnique::SIMPLE;
    float depthBiasConstantFactor = 0.5f;
    float depthBiasClamp = 0.f;
    float depthBiasSlopeFactor = 3.00f;

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
  std::optional<MeshPipeline> vegetationMeshPipeline{};
  etna::ComputePipeline generateClipmapPipeline{};
  etna::ComputePipeline resetTerrainChunkHeightBoundsPipeline{};
  etna::ComputePipeline generateTerrainChunkHeightBoundsPipeline{};
  etna::ComputePipeline transferTerrainChunkHeightBoundsPipeline{};
  etna::ComputePipeline transferLightMatsPipeline{};
  etna::ComputePipeline vegetationGenerateClearChunks{};
  etna::ComputePipeline vegetationGenerateCullChunks{};
  etna::ComputePipeline vegetationGenerateSortChunks{};
  etna::ComputePipeline vegetationGeneratePrepareInstCommand{};
  etna::ComputePipeline vegetationGenerateInstances{};

  std::vector<std::unique_ptr<IComponent>> rcomponents{};

  std::array<ITonemapper*, TONEMAPPING_TECHNIQUE_COUNT> tonemapperComps{};

  etna::Image hdrTarget;
  etna::Image gbufAlbedo, gbufMaterial, gbufNormal;
  etna::Image gbufTransmission; // @SPEED piggy
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

  bool needRegenSsaoKernel = false;

  UniformLights prevLights{};

  std::optional<TerrainRenderingData> terrain{};
  std::optional<VegetationRenderingData> vegetation{};
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

  int frame = -1;

  glm::uvec2 resolution;
  const Config& cfg;

  etna::Image ssaoBuffer;
  etna::Image ssaoBlurredBuffer;
  std::unique_ptr<PostfxRenderer> ssaoGen{};
  std::unique_ptr<PostfxRenderer> ssaoBlur{};

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
  bool drawVegetation = true;
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
  float vegetationRenderingDistance = 100.f;
  float vegetationRenderingDropoffDistance = 80.f;
  glm::vec2 windOrigin = {0.f, 0.f};
  float windStrength = 0.35f;
  float histEqTonemappingRegW = 0.5f, histEqTonemappingRefinedW = 0.5f;
  // @TODO: find a way to deal with jittering from lum outliers?
  float histEqTonemappingMinAdmissibleLum = 0.0f, histEqTonemappingMaxAdmissibleLum = 10.f;
  float acesExposure = 2.f;
  float csmSplitLambda = 0.5f;
  float csmShadowDist = 400.f;
  float csmBlendingBeltSize = 0.03f;
  TonemappingTechnique currentTonemappingTechnique = TonemappingTechnique::ACES;
  bool zPrepass = true;
  bool sortVegChunks = true;
  bool useSsao = true;
  glm::vec3 ambientCoeff = glm::vec3(0.3f);
  bool useSkyboxForAmbient = true;
  bool showGrassChunkDebug = false;
  bool showSsaoKernelDebug = false;
  bool ssaoKernelHemisphereOnly = true;
  float ssaoKernelRadius = 0.5f;
  float ssaoBias = 0.025f;
  uint32_t ssaoTotalLimitSamples = 64;

  MovingAverageAccumulator<float, 64> smoothedDt{};

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

  uint32_t vegChunkBufferSizeBytes() const
  {
    return 2 * sizeof(int32_t) +
      VEGETATION_GRID_EXTENT * VEGETATION_GRID_EXTENT * sizeof(shader_vec2);
  }
  uint32_t vegInstBufferSizeBytes() const
  {
    return VEGETATION_GRID_EXTENT * VEGETATION_GRID_EXTENT *
      sceneMgr->getVegetationTemplateData().size() * sizeof(GrassInstance);
  }

  void generateSsaoKernel(std::span<glm::vec4> out_samples);
};
