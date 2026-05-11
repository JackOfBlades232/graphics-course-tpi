#pragma once

#include <etna/Vulkan.hpp>
#include <etna/Image.hpp>
#include <etna/GpuWorkCount.hpp>
#include <function2/function2.hpp>

/**
 * Class for managing a double buffered (i.e. single hist) Image
 */
class DoubleBufferedImage
{
public:
  struct CreateInfo
  {
    const etna::GpuWorkCount* wc;
  };

  DoubleBufferedImage() = default;

  explicit DoubleBufferedImage(CreateInfo info)
    : wc{info.wc}
  {
  }

  DoubleBufferedImage(DoubleBufferedImage&&) = default;
  DoubleBufferedImage& operator=(DoubleBufferedImage&&) = default;

  DoubleBufferedImage(const DoubleBufferedImage&) = delete;
  DoubleBufferedImage& operator=(const DoubleBufferedImage&) = delete;

  int curId() const { return wc->batchIndex() & 1; }
  int prevId() const { return 1 & ~curId(); }
  etna::Image& curBuf() { return buf[curId()]; }
  etna::Image& prevBuf() { return buf[prevId()]; }
  const etna::Image& curBuf() const { return buf[curId()]; }
  const etna::Image& prevBuf() const { return buf[prevId()]; }

private:
  etna::Image buf[2];
  const etna::GpuWorkCount* wc = nullptr;
};
