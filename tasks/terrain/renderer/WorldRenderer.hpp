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

  etna::Image gbufAlbedo, gbufMaterial, gbufNormal;
  etna::Image mainViewDepth;

  std::optional<etna::GpuSharedResource<etna::Buffer>> constants;
  std::optional<etna::GpuSharedResource<etna::Buffer>> lights;

  // @TODO: multiplexed & tweakable
  std::optional<etna::Buffer> terrainSource;

  etna::Buffer culledInstancesBuf;

  // @TODO: unify with one in scene manager
  etna::Sampler defaultSampler;

  etna::PersistentDescriptorSet materialParamsDset, bindlessTexturesDset, bindlessSamplersDset;
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
  std::map<std::string, const etna::Image *> debugTextures{};
  std::optional<std::string> currentDebugTex{};
  bool settingsGuiEnabled = false;
  bool drawBboxes = false;
};
