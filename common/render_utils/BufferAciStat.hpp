#pragma once

#include "shaders/draw.h"

#include <utils/Common.hpp>

#include <etna/Vulkan.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Buffer.hpp>

#include <string>

enum class BufferAciStatOp
{
  MINMAXZ
};

namespace detail
{

template <class T, BufferAciStatOp Op>
struct BufferAciStatName;
template <>
struct BufferAciStatName<DrawableInstance, BufferAciStatOp::MINMAXZ>
{
  static constexpr std::string_view name = "buffer_minmaxz_drawinst";
};

// @TODO add constraint

} // namespace detail

template <class T, BufferAciStatOp Op>
class BufferAciStatCollector
{
public:
  BufferAciStatCollector();

  void collect(
    vk::CommandBuffer cmd_buf,
    const etna::Buffer& src_buffer,
    uint32_t src_size,
    const vk::BufferMemoryBarrier2& src_transition_barrier,
    etna::Buffer& res_buffer,
    uint32_t res_size,
    const vk::BufferMemoryBarrier2& res_transition_barrier);

private:
  etna::ComputePipeline pipeline;
  etna::ShaderProgramId programId;
};
