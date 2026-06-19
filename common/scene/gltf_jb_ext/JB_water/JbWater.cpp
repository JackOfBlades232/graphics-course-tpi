#include "JbWater.hpp"

#include <spdlog/spdlog.h>

// @TODO: pull out
#define LOG(fmt_, ...) spdlog::info("[JB_water]: " fmt_ __VA_OPT__(, ) __VA_ARGS__)
#define FAIL(fmt_, ...)                                                                            \
  do                                                                                               \
  {                                                                                                \
    spdlog::error("[JB_water]: " fmt_ __VA_OPT__(, ) __VA_ARGS__);                                 \
    exit(1);                                                                                       \
  } while (0)
#define VERIFY(e_, fmt_, ...)                                                                      \
  do                                                                                               \
  {                                                                                                \
    if (!(e_))                                                                                     \
      FAIL(fmt_ __VA_OPT__(, ) __VA_ARGS__);                                                       \
  } while (0)

std::optional<JbWaterExtData> jb_water_parse_desc(const tinygltf::Model& model)
{
  if (!model.extensions.contains("JB_water"))
    return std::nullopt;

  const auto& desc = model.extensions.at("JB_water");

  JbWaterExtData data{};

  VERIFY(desc.Has("level"), "invalid format: must have a \"level\"");
  const auto& wl = desc.Get("level");
  VERIFY(wl.IsNumber(), "invalid format: \"level\" must be a number");
  data.waterLevel = float(wl.GetNumberAsDouble());

  VERIFY(desc.Has("cascades"), "invalid format: must have \"cascades\"");
  const auto& cs = desc.Get("cascades");
  VERIFY(cs.IsArray(), "invalid format: \"cascades\" must be an array of sizes");
  for (size_t i = 0; i < cs.ArrayLen(); ++i)
  {
    const auto& elem = cs.Get(i);
    VERIFY(elem.IsNumber(), "invalid format: \"cascades\" must be an array of number sizes");
    data.cascades.push_back(float(elem.GetNumberAsDouble()));
  }

  return data;
}
