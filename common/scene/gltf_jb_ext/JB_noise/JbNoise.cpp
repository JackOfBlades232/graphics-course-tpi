#include "JbNoise.hpp"

#include <spdlog/spdlog.h>

// @TODO: pull out
#define LOG(fmt_, ...) spdlog::info("[JB_noise]: " fmt_ __VA_OPT__(, ) __VA_ARGS__)
#define FAIL(fmt_, ...)                                                                            \
  do                                                                                               \
  {                                                                                                \
    spdlog::error("[JB_noise]: " fmt_ __VA_OPT__(, ) __VA_ARGS__);                                \
    exit(1);                                                                                       \
  } while (0)
#define VERIFY(e_, fmt_, ...)                                                                      \
  do                                                                                               \
  {                                                                                                \
    if (!(e_))                                                                                     \
      FAIL(fmt_ __VA_OPT__(, ) __VA_ARGS__);                                                       \
  } while (0)

static int get_texture(const auto& desc, const char* name)
{
  const auto& tex = desc.Get(name);
  VERIFY(
    tex.IsObject() && tex.Has("index"),
    "invalid format: \"{}\" must be a {{ \"index\" : N }}",
    name);
  const auto& ind = tex.Get("index");
  VERIFY(ind.IsNumber(), "invalid format: texture index for \"{}\" must be an int", name);
  return ind.GetNumberAsInt();
}

std::optional<JbNoiseExtData> jb_noise_parse_desc(const tinygltf::Model& model)
{
  if (!model.extensions.contains("JB_noise"))
    return std::nullopt;

  const auto& desc = model.extensions.at("JB_noise");

  JbNoiseExtData data{};

  if (desc.Has("blue"))
  {
    const auto& ns = desc.Get("blue");
    VERIFY(ns.IsObject(), "invalid format: \"blue\" must be a subobject");
    if (ns.Has("tex"))
      data.blue.tex = get_texture(ns, "tex");
  }

  return data;
}
