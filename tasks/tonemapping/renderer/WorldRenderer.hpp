#pragma once

#include <unordered_map>

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
#include <glm/glm.hpp>
#include <function2/function2.hpp>

#include <constants.h>

#include <render_utils/PostfxRenderer.hpp>
#include <render_utils/BboxRenderer.hpp>
#include <render_utils/QuadRenderer.hpp>
#include <scene/SceneManager.hpp>

#include <wsi/Keyboard.hpp>
#include <wsi/Mouse.hpp>

#include "FramePacket.hpp"
#include "Config.hpp"


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
  struct MeshPipeline
  {
    etna::GraphicsPipeline mainPipeline;
    etna::GraphicsPipeline wireframePipeline;

    MeshPipeline(
      etna::PipelineManager& pipeman,
      const char* prog_name,
      const etna::GraphicsPipeline::CreateInfo& ci);

    MeshPipeline() = default;
    MeshPipeline(MeshPipeline&&) = default;
    MeshPipeline& operator=(MeshPipeline&&) = default;

    const etna::GraphicsPipeline& get(bool wf) const
    {
      return wf ? wireframePipeline : mainPipeline;
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

    etna::Sampler clipmapSampler{};

    bool needToroidalUpdate = false;
  };

  struct DebugDrawer
  {
    using DrawRoutine = fu2::unique_function<void(
      vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)>;
    using SettingsRoutine = fu2::unique_function<void()>;

    DrawRoutine draw;
    SettingsRoutine settings;
  };

private:
  std::unique_ptr<SceneManager> sceneMgr;

  std::unique_ptr<PostfxRenderer> gbufferResolver{};
  std::unique_ptr<PostfxRenderer> tonemapper{};
  MeshPipeline staticMeshPipeline{};
  MeshPipeline terrainMeshPipeline{};
  etna::ComputePipeline cullingPipeline{};
  etna::ComputePipeline resetIndirectCommandsPipeline{};
  etna::ComputePipeline generateClipmapPipeline{};

  etna::ComputePipeline clearHistPipeline{};
  etna::ComputePipeline calculateHistMinmaxPipeline{};
  etna::ComputePipeline calculateHistDensityPipeline{};
  etna::ComputePipeline calculateHistDistributionPipeline{};

  etna::Image hdrTarget;
  etna::Image gbufAlbedo, gbufMaterial, gbufNormal;
  etna::Image mainViewDepth;

  etna::Buffer histData;

  std::optional<etna::GpuSharedResource<etna::Buffer>> constants;
  std::optional<etna::GpuSharedResource<etna::Buffer>> lights;

  std::optional<TerrainRenderingData> terrain{};

  // @TODO: tweakable
  etna::Buffer culledInstancesBuf;

  // @TODO: unify with one in scene manager
  etna::Sampler defaultSampler;

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
  bool directionalLightsAreOn = true;
  bool pointLightsAreOn = true;
  bool spotLightsAreOn = true;

  std::unique_ptr<BboxRenderer> bboxRenderer{};
  std::unique_ptr<QuadRenderer> quadRenderer{};
  // @TODO: add abstraction to drawing whatever to part of viewport
  etna::GraphicsPipeline histogramDebugPipeline{};
  std::map<std::string, DebugDrawer> debugDrawers{};

  std::optional<std::string> currentDebugDrawer{};
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
  bool doSatCulling = true;
  bool doTonemapping = true;
  bool useSharedMemForTonemapping = false;

private:
  void createManagedImage(etna::Image& dst, etna::Image::CreateInfo&& ci);
  void registerManagedImage(
    const etna::Image& img, std::optional<std::string> name_override = std::nullopt);

  void loadDebugConfig();
  void saveDebugConfig();

  void setAllDirLightsIntensity(float val);
  void setAllPointLightsIntensity(float val);
  void setAllSpotLightsIntensity(float val);
};
