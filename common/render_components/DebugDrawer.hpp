#pragma once

#include <etna/Vulkan.hpp>
#include <function2/function2.hpp>

#include <map>
#include <string>


struct DebugDrawer
{
  using DrawRoutine = fu2::unique_function<void(
    vk::CommandBuffer cmd_buf, vk::Image target_image, vk::ImageView target_image_view)>;
  using SettingsRoutine = fu2::unique_function<void()>;

  DrawRoutine draw;
  SettingsRoutine settings;
};

using DebugDrawersRegistryKey = std::string;
using DebugDrawersRegistry = std::map<DebugDrawersRegistryKey, DebugDrawer>;
