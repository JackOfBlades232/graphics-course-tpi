#pragma once

#include "IComponent.hpp"

#include <etna/Buffer.hpp>
#include <etna/Image.hpp>
#include <etna/Sampler.hpp>


class IAntialiaser : public IComponent
{
public:
  // @TODO: extend interface, TAA needs more resources
  virtual void antialias(
    vk::CommandBuffer cmd_buff,
    vk::Image target_image,
    vk::ImageView target_image_view,
    const etna::Image& aliased_image,
    const etna::Sampler& sampler,
    const etna::Buffer& constants) = 0;
};

