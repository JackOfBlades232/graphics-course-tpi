#pragma once

#include <etna/Image.hpp>
#include <etna/Sampler.hpp>
#include <etna/Buffer.hpp>
#include <etna/GraphicsPipeline.hpp>
#include <glm/glm.hpp>

#include <constants.h>

#include "render_utils/PostfxRenderer.hpp"
#include "scene/SceneManager.hpp"

#include "wsi/Keyboard.hpp"
#include "wsi/Mouse.hpp"

#include "FramePacket.hpp"


class WorldRenderer
{
public:
  explicit WorldRenderer(const etna::GpuWorkCount& wc);

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
  void renderScene(
    vk::CommandBuffer cmd_buf, const glm::mat4x4& glob_tm, vk::PipelineLayout pipeline_layout);

private:
  std::unique_ptr<SceneManager> sceneMgr;
  std::unique_ptr<PostfxRenderer> gbufferResolver{};

  etna::Image gbufAlbedo, gbufMaterial, gbufNormal;
  etna::Image mainViewDepth;

  std::optional<etna::GpuSharedResource<etna::Buffer>> constants;
  std::optional<etna::GpuSharedResource<etna::Buffer>> lights;

  // @TODO: unify with one in scene manager
  etna::Sampler defaultSampler;

  etna::PersistentDescriptorSet bindlessDset;
  bool transitionedBindlessLayouts = false; // @HACK

  const etna::GpuWorkCount& wc;

  struct PushConstantsMesh
  {
    glm::mat4x4 projView;
    glm::mat4x4 modelAndMatId;
  } pushConstMesh;

  struct PushConstantsResolve
  {
    glm::mat4x4 proj;
    glm::mat4x4 view;
  } pushConstResolve;

  // @TODO: what are guarantees for in-command buffer changes & move data here
  Constants constantsData = {};

  glm::mat4x4 worldViewProj;
  glm::mat4x4 worldView;
  glm::mat4x4 proj;

  float prevTime = -1.f;
  float dt = 0.f;

  etna::GraphicsPipeline staticMeshPipeline{};

  glm::uvec2 resolution;
  bool settingsGuiEnabled = false;
};
