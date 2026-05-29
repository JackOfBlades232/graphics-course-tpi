#pragma once

#include "IAntialiaser.hpp"
#include "DebugDrawer.hpp"

#include <render_utils/PostfxRenderer.hpp>

#include <etna/GraphicsPipeline.hpp>
#include <function2/function2.hpp>

class TAAAntialiaser final : public IAntialiaser
{
  using image_bind_provider_t = fu2::unique_function<etna::ImageBinding()>;
  struct ResourceProviderCallbacks
  {
    image_bind_provider_t prevFrameProvider;
    image_bind_provider_t motionVectorsProvider;
    image_bind_provider_t depthProvider;
  };

public:
  struct CreateInfo
  {
    ResourceProviderCallbacks cb;
  };

  TAAAntialiaser(CreateInfo&& ci)
    : resourceCb{std::move(ci.cb)}
  {
  }

  void allocateResources(glm::uvec2 resolution) final;
  void loadShaders() final {}
  void setupPipelines(vk::Format swapchain_format, DebugDrawersRegistry&) final;

  void antialias(
    vk::CommandBuffer cmd_buf,
    vk::Image target_image,
    vk::ImageView target_image_view,
    const etna::Image& aliased_image,
    const etna::Sampler& sampler,
    const etna::Buffer& constants) final;

private:
  std::unique_ptr<PostfxRenderer> aa{};
  glm::uvec2 targetResolution{};
  ResourceProviderCallbacks resourceCb;
};
