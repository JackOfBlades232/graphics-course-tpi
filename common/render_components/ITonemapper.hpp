#pragma once

#include "IComponent.hpp"

#include <etna/Buffer.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>


class ITonemapper : public IComponent
{
public:
  virtual void tonemap(
    vk::CommandBuffer cmd_buff,
    vk::Image target_image,
    vk::ImageView target_image_view,
    const etna::Image& hdr_image,
    const etna::Sampler& sampler,
    const etna::Buffer& constants) = 0;
};
