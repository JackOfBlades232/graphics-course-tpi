#pragma once

#include <utils/Common.hpp>

#include <etna/Vulkan.hpp>
#include <etna/ComputePipeline.hpp>
#include <etna/Buffer.hpp>

#include <string>

namespace detail
{

template <class T>
struct BitonicSortName;
template <>
struct BitonicSortName<float>
{
  static constexpr std::string_view name = "bitonic_sort_float";
};

// @TODO: fix msvc and restore
#define BITONIC_SORTER_CONSTRAINT(T_) //requires(requires() { detail::BitonicSortName<T_>::name; })

} // namespace detail

template <class T>
BITONIC_SORTER_CONSTRAINT(T)
class BitonicSorter
{
public:
  BitonicSorter();

  void sort(
    vk::CommandBuffer cmd_buf,
    etna::Buffer& buffer,
    uint32_t size,
    const vk::BufferMemoryBarrier2& transition_barrier)
  {
    ETNA_ASSERT(is_pot(size)); // @TODO: impl npot
    sortPotImpl(cmd_buf, buffer, size, transition_barrier);
  }

private:
  etna::ComputePipeline pipeline;
  etna::ShaderProgramId programId;

private:
  void sortPotImpl(
    vk::CommandBuffer cmd_buf,
    etna::Buffer& buffer,
    uint32_t size,
    const vk::BufferMemoryBarrier2& transition_barrier);
};
