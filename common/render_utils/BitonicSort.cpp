#include "BitonicSort.hpp"

#include "shaders/defs.h"
#include "shaders/cpp_glsl_compat.h"
#include "shaders/draw.h"

#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/GlobalContext.hpp>

template <class T>
  requires(requires() { detail::BitonicSortName<T>::name; })
BitonicSorter<T>::BitonicSorter()
{
  constexpr std::string_view sorterName = detail::BitonicSortName<T>::name;
  const std::string progName = std::string{sorterName};
  const std::string shaderPath = fmt::format(RENDER_UTILS_SHADERS_ROOT "{}.comp.spv", sorterName);

  programId = etna::get_program_id(progName.c_str());
  if (programId == etna::ShaderProgramId::Invalid)
    programId = etna::create_program(progName.c_str(), {shaderPath.c_str()});

  auto& pipelineManager = etna::get_context().getPipelineManager();
  pipeline = pipelineManager.createComputePipeline(progName.c_str(), {});
}

static void emit_barrier(vk::CommandBuffer cmd_buf, const vk::BufferMemoryBarrier2& bar)
{
  cmd_buf.pipelineBarrier2(vk::DependencyInfo{
    .dependencyFlags = vk::DependencyFlagBits::eByRegion,
    .bufferMemoryBarrierCount = 1,
    .pBufferMemoryBarriers = &bar});
}

template <class T>
  requires(requires() { detail::BitonicSortName<T>::name; })
void BitonicSorter<T>::sortPotImpl(
  vk::CommandBuffer cmd_buf,
  etna::Buffer& buffer,
  uint32_t size,
  const vk::BufferMemoryBarrier2& transition_barrier)
{
  emit_barrier(
    cmd_buf,
    vk::BufferMemoryBarrier2{
      .srcStageMask = transition_barrier.srcStageMask,
      .srcAccessMask = transition_barrier.srcAccessMask,
      .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
      .dstAccessMask =
        vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
      .buffer = buffer.get(),
      .size = sizeof(T) * size});

  auto programInfo = etna::get_shader_program(programId);
  auto set = etna::create_descriptor_set(
    programInfo.getDescriptorLayoutId(0), cmd_buf, {etna::Binding{0, buffer.genBinding()}});

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline.getVkPipelineLayout(), 0, {set.getVkSet()}, {});
  cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.getVkPipeline());

  const uint32_t stageCnt = next_pot_pow(size);

  for (uint32_t stage = 0; stage < stageCnt; ++stage)
    for (uint32_t pass = 0; pass <= stage; ++pass)
    {
      struct BitonicSortPushConsts
      {
        shader_uint shiftPow;
        shader_uint dirSwitchRunPow;
      } pushConstants{stage - pass, stage};

      cmd_buf.pushConstants<BitonicSortPushConsts>(
        pipeline.getVkPipelineLayout(), vk::ShaderStageFlagBits::eCompute, 0, pushConstants);

      cmd_buf.dispatch(get_linear_wg_count(1u << (stageCnt - 1), BASE_WORK_GROUP_SIZE), 1, 1);

      emit_barrier(
        cmd_buf,
        vk::BufferMemoryBarrier2{
          .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .srcAccessMask =
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
          .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
          .dstAccessMask =
            vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
          .buffer = buffer.get(),
          .size = sizeof(T) * size});
    }

  if (
    (transition_barrier.dstStageMask & ~vk::PipelineStageFlagBits2::eComputeShader) ||
    (transition_barrier.dstAccessMask &
     ~(vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite)))
  {
    emit_barrier(
      cmd_buf,
      vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask =
          vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = transition_barrier.dstStageMask,
        .dstAccessMask = transition_barrier.dstAccessMask,
        .buffer = buffer.get(),
        .size = sizeof(T) * size});
  }
}

// Explicit instantiations. Must match all declared specs for BitonicSortName.

template class BitonicSorter<float>;
