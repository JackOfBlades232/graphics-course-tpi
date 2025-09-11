#include "BufferAciStat.hpp"
#include "Common.hpp"

#include "shaders/defs.h"
#include "shaders/cpp_glsl_compat.h"
#include "shaders/draw.h"

#include <etna/Etna.hpp>
#include <etna/PipelineManager.hpp>
#include <etna/GlobalContext.hpp>

template <class T, BufferAciStatOp Op>
BufferAciStatCollector<T, Op>::BufferAciStatCollector()
{
  constexpr std::string_view sorterName = detail::BufferAciStatName<T, Op>::name;
  const std::string progName = std::string{sorterName};
  const std::string shaderPath = fmt::format(RENDER_UTILS_SHADERS_ROOT "{}.comp.spv", sorterName);

  programId = etna::get_program_id(progName.c_str());
  if (programId == etna::ShaderProgramId::Invalid)
    programId = etna::create_program(progName.c_str(), {shaderPath.c_str()});

  auto& pipelineManager = etna::get_context().getPipelineManager();
  pipeline = pipelineManager.createComputePipeline(progName.c_str(), {});
}

template <class T, BufferAciStatOp Op>
void BufferAciStatCollector<T, Op>::collect(
  vk::CommandBuffer cmd_buf,
  const etna::Buffer& src_buffer,
  uint32_t src_size,
  const vk::BufferMemoryBarrier2& src_transition_barrier,
  etna::Buffer& res_buffer,
  uint32_t res_size,
  const vk::BufferMemoryBarrier2& res_transition_barrier)
{
  emit_barriers(
    cmd_buf,
    {vk::BufferMemoryBarrier2{
       .srcStageMask = src_transition_barrier.srcStageMask,
       .srcAccessMask = src_transition_barrier.srcAccessMask,
       .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .dstAccessMask = vk::AccessFlagBits2::eShaderStorageRead,
       .buffer = src_buffer.get(),
       .size = sizeof(T) * src_size},
     vk::BufferMemoryBarrier2{
       .srcStageMask = res_transition_barrier.srcStageMask,
       .srcAccessMask = res_transition_barrier.srcAccessMask,
       .dstStageMask = vk::PipelineStageFlagBits2::eComputeShader,
       .dstAccessMask =
         vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
       .buffer = res_buffer.get(),
       .size = sizeof(T) * res_size}});

  auto programInfo = etna::get_shader_program(programId);
  auto set = etna::create_descriptor_set(
    programInfo.getDescriptorLayoutId(0),
    cmd_buf,
    {etna::Binding{0, src_buffer.genBinding()}, etna::Binding{1, res_buffer.genBinding()}});

  cmd_buf.bindDescriptorSets(
    vk::PipelineBindPoint::eCompute, pipeline.getVkPipelineLayout(), 0, {set.getVkSet()}, {});
  cmd_buf.bindPipeline(vk::PipelineBindPoint::eCompute, pipeline.getVkPipeline());

  cmd_buf.dispatch(
    get_linear_wg_count(src_size, BASE_WORK_GROUP_SIZE * BUFFER_ACI_ELEMS_PER_THREAD), 1, 1);

  if (
    (src_transition_barrier.dstStageMask & ~vk::PipelineStageFlagBits2::eComputeShader) ||
    (src_transition_barrier.dstAccessMask & ~vk::AccessFlagBits2::eShaderStorageRead))
  {
    emit_barriers(
      cmd_buf,
      {vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask =
          vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = src_transition_barrier.dstStageMask,
        .dstAccessMask = src_transition_barrier.dstAccessMask,
        .buffer = src_buffer.get(),
        .size = sizeof(T) * src_size}});
  }
  if (
    (res_transition_barrier.dstStageMask & ~vk::PipelineStageFlagBits2::eComputeShader) ||
    (res_transition_barrier.dstAccessMask &
     ~(vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite)))
  {
    emit_barriers(
      cmd_buf,
      {vk::BufferMemoryBarrier2{
        .srcStageMask = vk::PipelineStageFlagBits2::eComputeShader,
        .srcAccessMask =
          vk::AccessFlagBits2::eShaderStorageRead | vk::AccessFlagBits2::eShaderStorageWrite,
        .dstStageMask = res_transition_barrier.dstStageMask,
        .dstAccessMask = res_transition_barrier.dstAccessMask,
        .buffer = res_buffer.get(),
        .size = sizeof(T) * res_size}});
  }
}

// Explicit instantiations. Must match all declared specs for BufferAciStatName.

template class BufferAciStatCollector<DrawableInstance, BufferAciStatOp::MINMAXZ>;
