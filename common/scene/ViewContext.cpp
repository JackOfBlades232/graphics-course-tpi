#include "ViewContext.hpp"

#include <render_utils/Common.hpp>

#include <etna/PipelineManager.hpp>
#include <etna/Profiling.hpp>


void ViewContextManager::loadShaders()
{
  etna::create_program("culling", {SCENE_SHADERS_ROOT "culling.comp.spv"});
  etna::create_program("reset_view_context", {SCENE_SHADERS_ROOT "reset_view_context.comp.spv"});
  etna::create_program("reset_indirect_buf", {SCENE_SHADERS_ROOT "reset_indirect_buf.comp.spv"});
  etna::create_program(
    "calculate_depth_bounds", {SCENE_SHADERS_ROOT "calculate_depth_bounds.comp.spv"});
}

void ViewContextManager::setupPipelines(vk::Format, DebugDrawersRegistry&)
{
  auto& pipelineManager = etna::get_context().getPipelineManager();
  cullingPipeline = pipelineManager.createComputePipeline("culling", {});
  resetViewContextPipeline = pipelineManager.createComputePipeline("reset_view_context", {});
  resetIndirectBufPipeline = pipelineManager.createComputePipeline("reset_indirect_buf", {});
  calculateDepthBoundsPipeline =
    pipelineManager.createComputePipeline("calculate_depth_bounds", {});
}

ViewContext ViewContextManager::alloc(const char* tag)
{
  return ViewContext{
    .indirectDrawBuf = create_buffer(etna::Buffer::CreateInfo{
      .size = indirectDrawBufByteSize(),
      .bufferUsage = vk::BufferUsageFlagBits::eTransferDst |
        vk::BufferUsageFlagBits::eStorageBuffer | vk::BufferUsageFlagBits::eIndirectBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = std::string{"indirectDrawBuf-"} + tag,
    }),
    .culledInstancesBuf = create_buffer(etna::Buffer::CreateInfo{
      .size = markedInstBufSizeBytes(),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = std::string{"culledInstancesBuf-"} + tag,
    }),
    .viewDataBuf = create_buffer(etna::Buffer::CreateInfo{
      .size = sizeof(ViewData),
      .bufferUsage = vk::BufferUsageFlagBits::eStorageBuffer,
      .memoryUsage = VMA_MEMORY_USAGE_GPU_ONLY,
      .name = std::string{"viewData-"} + tag,
    }),
    .viewParamsBuf =
      etna::GpuSharedResource<etna::Buffer>{
        workCount,
        [&](size_t) {
          return create_buffer(etna::Buffer::CreateInfo{
            .size = sizeof(ViewParams),
            .bufferUsage = vk::BufferUsageFlagBits::eUniformBuffer,
            .memoryUsage = VMA_MEMORY_USAGE_CPU_ONLY,
            .allocationCreate = VMA_ALLOCATION_CREATE_MAPPED_BIT,
            .name = std::string{"viewParams-"} + tag});
        }},
    .prepared = false};
}

void ViewContextManager::cullForView(
  vk::CommandBuffer cmd_buf,
  ViewContext& ctx,
  const ViewParams& params,
  const etna::Buffer& constants)
{
  ETNA_PROFILE_GPU(cmd_buf, culling); // @TODO: spec tags

  if (!ctx.prepared)
  {
    emit_barriers(
      cmd_buf,
      {vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
        .srcAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .dstAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .buffer = ctx.indirectDrawBuf.get(),
        .size = indirectDrawBufByteSize()}});

    cmd_buf.copyBuffer(
      sceneMgr.getIndirectCommandsBuf().get(),
      ctx.indirectDrawBuf.get(),
      {{0, 0, indirectDrawBufByteSize()}});

    ctx.prepared = true;

    emit_barriers(
      cmd_buf,
      {vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eTransfer,
        .srcAccessMask = vk::AccessFlagBits2::eTransferWrite,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .buffer = ctx.indirectDrawBuf.get(),
        .size = indirectDrawBufByteSize()}});
  }
  else
  {
    emit_barriers(
      cmd_buf,
      {vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
        .srcAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
        .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
        .buffer = ctx.indirectDrawBuf.get(),
        .size = indirectDrawBufByteSize()}});
  }

  emit_barriers(
    cmd_buf,
    {vk::BufferMemoryBarrier2{
      .srcStageMask = vk::PipelineStageFlagBits2::eVertexShader |
        vk::PipelineStageFlagBits2::eTessellationEvaluationShader |
        vk::PipelineStageFlagBits2::eComputeShader,
      .srcAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
      .buffer = ctx.viewDataBuf.get(),
      .size = sizeof(ViewData)}});

  {
    auto programInfo = etna::get_shader_program("reset_view_context");
    auto set = etna::create_descriptor_set(
      programInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {etna::Binding{0, ctx.indirectDrawBuf.genBinding()},
       etna::Binding{1, ctx.viewDataBuf.genBinding()}});
    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute,
      resetViewContextPipeline.getVkPipelineLayout(),
      0,
      {set.getVkSet()},
      {});
    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, resetViewContextPipeline.getVkPipeline());

    cmd_buf.dispatch(
      get_linear_wg_count(
        uint32_t(std::max(sceneMgr.getIndirectCommands().size(), size_t(1))), BASE_WORK_GROUP_SIZE),
      1,
      1);
  }

  emit_barriers(
    cmd_buf,
    {vk::BufferMemoryBarrier2{
       .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
       .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .dstAccessMask =
         vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
       .buffer = ctx.indirectDrawBuf.get(),
       .size = indirectDrawBufByteSize()},
     vk::BufferMemoryBarrier2{
       .srcStageMask = vk::PipelineStageFlagBits2::eVertexShader,
       .srcAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
       .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .dstAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
       .buffer = ctx.culledInstancesBuf.get(),
       .size = markedInstBufSizeBytes()}});

  {
    ETNA_PROFILE_GPU(cmd_buf, culling);

    auto programInfo = etna::get_shader_program("culling");
    auto set = etna::create_descriptor_set(
      programInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {etna::Binding{0, sceneMgr.getInstanceMatricesBuf().genBinding()},
       etna::Binding{1, sceneMgr.getInstancesBuf().genBinding()},
       etna::Binding{2, sceneMgr.getBboxesBuf().genBinding()},
       etna::Binding{3, ctx.culledInstancesBuf.genBinding()},
       etna::Binding{4, ctx.indirectDrawBuf.genBinding()},
       etna::Binding{8, constants.genBinding()},
       etna::Binding{9, ctx.viewParamsBuf.get().genBinding()}});

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute,
      cullingPipeline.getVkPipelineLayout(),
      0,
      {set.getVkSet()},
      {});
    cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, cullingPipeline.getVkPipeline());

    cmd_buf.dispatch(
      get_linear_wg_count(uint32_t(sceneMgr.getInstances().size()), BASE_WORK_GROUP_SIZE), 1, 1);
  }

  if (params.needDepthBounds)
  {
    emit_barriers(
      cmd_buf,
      {vk::BufferMemoryBarrier2{
         .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .srcAccessMask =
           vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
         .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
         .buffer = ctx.indirectDrawBuf.get(),
         .size = indirectDrawBufByteSize()},
       vk::BufferMemoryBarrier2{
         .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
         .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
         .buffer = ctx.culledInstancesBuf.get(),
         .size = markedInstBufSizeBytes()},
       vk::BufferMemoryBarrier2{
         .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
         .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .dstAccessMask =
           vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
         .buffer = ctx.viewDataBuf.get(),
         .size = sizeof(ViewData)}});

    ETNA_PROFILE_GPU(cmd_buf, depthBounds);

    auto programInfo = etna::get_shader_program("calculate_depth_bounds");
    auto set = etna::create_descriptor_set(
      programInfo.getDescriptorLayoutId(0),
      cmd_buf,
      {etna::Binding{0, ctx.culledInstancesBuf.genBinding()},
       etna::Binding{1, ctx.indirectDrawBuf.genBinding()},
       etna::Binding{2, ctx.viewDataBuf.genBinding()}});

    cmd_buf.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute,
      calculateDepthBoundsPipeline.getVkPipelineLayout(),
      0,
      {set.getVkSet()},
      {});
    cmd_buf.bindPipeline(
      vk::PipelineBindPoint::eCompute, calculateDepthBoundsPipeline.getVkPipeline());

    cmd_buf.dispatch(
      get_linear_wg_count(
        uint32_t(sceneMgr.getInstances().size()),
        BASE_WORK_GROUP_SIZE * CALC_DEPTH_BOUNDS_ELEMS_PER_THREAD),
      1,
      1);

    emit_barriers(
      cmd_buf,
      {vk::BufferMemoryBarrier2{
         .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .srcAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
         .dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
         .dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
         .buffer = ctx.indirectDrawBuf.get(),
         .size = indirectDrawBufByteSize()},
       vk::BufferMemoryBarrier2{
         .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .srcAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
         .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader,
         .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
         .buffer = ctx.culledInstancesBuf.get(),
         .size = markedInstBufSizeBytes()},
       vk::BufferMemoryBarrier2{
         .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .srcAccessMask =
           vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
         .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader |
           vk::PipelineStageFlagBits2::eTessellationEvaluationShader |
           vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader,
         .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
         .buffer = ctx.viewDataBuf.get(),
         .size = sizeof(ViewData)}});
  }
  else
  {
    emit_barriers(
      cmd_buf,
      {vk::BufferMemoryBarrier2{
         .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .srcAccessMask =
           vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
         .dstStageMask = vk::PipelineStageFlagBits2::eDrawIndirect,
         .dstAccessMask = vk::AccessFlagBits2::eIndirectCommandRead,
         .buffer = ctx.indirectDrawBuf.get(),
         .size = indirectDrawBufByteSize()},
       vk::BufferMemoryBarrier2{
         .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
         .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader,
         .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
         .buffer = ctx.culledInstancesBuf.get(),
         .size = markedInstBufSizeBytes()},
       vk::BufferMemoryBarrier2{
         .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
         .srcAccessMask = vk::AccessFlagBits2::eShaderStorageWrite,
         .dstStageMask = vk::PipelineStageFlagBits2::eVertexShader |
           vk::PipelineStageFlagBits2::eTessellationEvaluationShader |
           vk::PipelineStageFlagBits2::eComputeShader | vk::PipelineStageFlagBits2::eFragmentShader,
         .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
         .buffer = ctx.viewDataBuf.get(),
         .size = sizeof(ViewData)}});
  }
}

void ViewContextManager::resetIndirectBufNoBarriers(
  vk::CommandBuffer cmd_buf, const etna::Buffer& buf, uint32_t cmd_count) const
{
  auto programInfo = etna::get_shader_program("reset_indirect_buf");
  auto set = etna::create_descriptor_set(
    programInfo.getDescriptorLayoutId(0), cmd_buf, {etna::Binding{0, buf.genBinding()}});
  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute,
    resetIndirectBufPipeline.getVkPipelineLayout(),
    0,
    {set.getVkSet()},
    {});
  cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, resetIndirectBufPipeline.getVkPipeline());
  cmd_buf.dispatch(
    get_linear_wg_count(std::max(cmd_count, uint32_t(1)), BASE_WORK_GROUP_SIZE), 1, 1);
}
