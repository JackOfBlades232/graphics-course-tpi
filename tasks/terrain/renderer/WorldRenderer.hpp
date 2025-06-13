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
  std::unique_ptr<SceneManager> sceneMgr;

  std::unique_ptr<PostfxRenderer> gbufferResolver{};
  etna::GraphicsPipeline staticMeshPipeline{};
  etna::ComputePipeline cullingPipeline{};
  etna::ComputePipeline resetIndirectCommandsPipeline{};
  etna::ComputePipeline generateClipmapPipeline{};

  etna::Image gbufAlbedo, gbufMaterial, gbufNormal;
  etna::Image mainViewDepth;

  std::optional<etna::GpuSharedResource<etna::Buffer>> constants;
  std::optional<etna::GpuSharedResource<etna::Buffer>> lights;

  struct TerrainRenderingData
  {
    etna::Image geometryClipmap{};
    etna::Image albedoClipmap{};
    std::vector<etna::Binding> geometryLevelsBindings{};
    std::vector<etna::Binding> albedoLevelsBindings{};
    etna::Buffer source{};

    TerrainSourceData sourceData{};

    glm::vec3 lastToroidalUpdatePlayerWorldPos = {};
    bool needToroidalUpdate = false;
  };
  std::optional<TerrainRenderingData> terrain{};


  // @TODO: tweakable
  etna::Buffer culledInstancesBuf;

  // @TODO: unify with one in scene manager
  etna::Sampler defaultSampler;

  etna::PersistentDescriptorSet
    materialParamsDsetFrag, bindlessTexturesDsetFrag, bindlessSamplersDsetFrag,
    materialParamsDsetComp, bindlessTexturesDsetComp, bindlessSamplersDsetComp;
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
};
