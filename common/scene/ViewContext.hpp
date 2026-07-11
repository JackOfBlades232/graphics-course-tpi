#pragma once

#include "SceneManager.hpp"

#include <render_components/IComponent.hpp>

#include <etna/Image.hpp>
#include <etna/Vulkan.hpp>
#include <etna/ComputePipeline.hpp>

// @TODO: rename to view
#include <geometry.h>

#include <string>


struct ViewContext
{
  etna::Buffer indirectDrawBuf;
  etna::Buffer culledInstancesBuf;
  etna::Buffer viewDataBuf;
  etna::GpuSharedResource<etna::Buffer> viewParamsBuf;
  bool prepared;

  void update(const ViewParams& params)
  {
    auto& buf = viewParamsBuf.get();
    memcpy(buf.data(), &params, sizeof(params));
  }
};

class ViewContextManager : public IComponent
{
public:
  ViewContextManager(const etna::GpuWorkCount& wc, const SceneManager& sm)
    : workCount{wc}
    , sceneMgr{sm}
  {
  }

  void allocateResources(glm::uvec2) final {}
  void loadShaders() final;
  void setupPipelines(vk::Format, DebugDrawersRegistry&) final;

  ViewContext alloc(const char* tag = "");
  void cullForView(
    vk::CommandBuffer cmd_buf,
    ViewContext& ctx,
    const ViewParams& params,
    const etna::Buffer& constants);

  void resetIndirectBufNoBarriers(
    vk::CommandBuffer cmd_buf, const etna::Buffer& buf, uint32_t cmd_count) const;

private:
  etna::ComputePipeline cullingPipeline{};
  etna::ComputePipeline calculateDepthBoundsPipeline{};
  etna::ComputePipeline resetViewContextPipeline{};
  etna::ComputePipeline resetIndirectBufPipeline{};
  const etna::GpuWorkCount& workCount;
  const SceneManager& sceneMgr;

private:
  uint32_t indirectDrawBufByteSize() const { return uint32_t(sceneMgr.getIndirectCommands().size_bytes()); }
  uint32_t markedInstBufSizeBytes() const
  {
    return uint32_t(sceneMgr.getInstances().size() * sizeof(DrawableInstance));
  }
};
