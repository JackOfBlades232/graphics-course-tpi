#pragma once

#include <unordered_map>

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <etna/ComputePipeline.hpp>
#include <glm/glm.hpp>

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
    std::vector<etna::Binding> geometryLevelsBindings{};
    std::vector<etna::Binding> normalLevelsBindings{};
    std::vector<etna::Binding> albedoLevelsBindings{};
    std::vector<etna::Binding> geometryLevelsSamplerBindings{};
    std::vector<etna::Binding> normalLevelsSamplerBindings{};
    std::vector<etna::Binding> albedoLevelsSamplerBindings{};

    etna::Buffer source{};
    TerrainSourceData sourceData{};

    etna::Sampler clipmapSampler{};

    bool needToroidalUpdate = false;
  };

  std::unique_ptr<SceneManager> sceneMgr;

  std::unique_ptr<PostfxRenderer> gbufferResolver{};
  MeshPipeline staticMeshPipeline{};
  MeshPipeline terrainMeshPipeline{};
  etna::ComputePipeline cullingPipeline{};
  etna::ComputePipeline resetIndirectCommandsPipeline{};
  etna::ComputePipeline generateClipmapPipeline{};

  etna::Image gbufAlbedo, gbufMaterial, gbufNormal;
  etna::Image mainViewDepth;

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
  std::unique_ptr<BboxRenderer> bboxRenderer{};
  std::unique_ptr<QuadRenderer> quadRenderer{};
  std::map<std::string, const etna::Image*> debugTextures{};
  std::optional<std::string> currentDebugTex{};
  uint32_t currentDebugTexMip = 0;
  uint32_t currentDebugTexLayer = 0;
  bool settingsGuiEnabled = false;
  bool drawBboxes = false;
  bool wireframe = false;
};
