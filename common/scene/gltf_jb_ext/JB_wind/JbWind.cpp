#include "JbWind.hpp"

#include <spdlog/spdlog.h>

// @TODO: pull out
#define LOG(fmt_, ...) spdlog::info("[JB_wind]: " fmt_ __VA_OPT__(, ) __VA_ARGS__)
#define FAIL(fmt_, ...)                                                                            \
  do                                                                                               \
  {                                                                                                \
    spdlog::error("[JB_wind]: " fmt_ __VA_OPT__(, ) __VA_ARGS__);                                  \
    exit(1);                                                                                       \
  } while (0)
#define VERIFY(e_, fmt_, ...)                                                                      \
  do                                                                                               \
  {                                                                                                \
    if (!(e_))                                                                                     \
      FAIL(fmt_ __VA_OPT__(, ) __VA_ARGS__);                                                       \
  } while (0)

template <class T>
struct Attribute
{
  T value;
  std::string error;
};

static Attribute<const tinygltf::Object*> get_subobject(
  const tinygltf::Object& obj, const char* name)
{
  if (elem.Has(name))
    return {&obj.Get(name), {}};
  else
    return {nullptr, fmt::format("Missing attribute \"{}\"", name)};
}

static Attribute<glm::vec2> get_vec2(const tinygltf::Object& obj, const char* name)
{
  auto attr = get_subobject(obj, name);
  if (const auto* obj = attr.value)
  {
    if (!obj.IsArray() || obj.ArrayLen() != 2)
      return {nullptr, fmt::format("\"{}\" must be a 2d array", name)};
    glm::vec2 v{};
    float* p = (float*)&v;
    for (int i = 0; i < 2; ++i, ++p)
    {
      const auto& elem = obj.Get(i);
      if (!elem.IsNumber())
        return {nullptr, fmt::format("\"{}\" must be a vector of doubles", name)};
      *p = float(elem.GetNumberAsDouble());
    }
    return {v, {}};
  }
  else
  {
    return {{}, std::move(attr.error)};
  }
}

static Attribute<float> get_float(const tinygltf::Object& obj, const char* name)
{
  auto attr = get_subobject(obj, name);
  if (const auto* obj = attr.value)
  {
    if (!obj.IsNumber())
      return {nullptr, fmt::format("\"{}\" must be a double", name)};
    return {float(obj.GetNumberAsDouble()), {}};
  }
  else
  {
    return {{}, std::move(attr.error)};
  }
}

std::optional<JbWindExtData> jb_wind_parse_desc(const tinygltf::Model& model)
{
  if (!model.extensions.contains("JB_wind"))
    return std::nullopt;

  const auto& desc = model.extensions.at("JB_wind");

  JbWindExtData data{};
  auto dir = get_vec2(desc, "direction");
  auto str = get_float(desc, "strength");
  if (!dir.error.empty())
    FAIL("{}", dir.error);
  if (!str.error.empty())
    FAIL("{}", str.error);
  data.direction = dir.value;
  data.strength = str.value;

  return data;
}
