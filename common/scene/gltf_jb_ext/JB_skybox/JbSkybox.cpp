#include "JbSkybox.hpp"

#include <spdlog/spdlog.h>

// @TODO: pull out
#define LOG(fmt_, ...) spdlog::info("[JB_skybox]: " fmt_ __VA_OPT__(, ) __VA_ARGS__)
#define FAIL(fmt_, ...)                                                                            \
  do                                                                                               \
  {                                                                                                \
    spdlog::error("[JB_skybox]: " fmt_ __VA_OPT__(, ) __VA_ARGS__);                                \
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

static int get_mandatory_texture(const auto& desc, const char* name)
{
  VERIFY(desc.Has(name), "invalid format: texture \"{}\" is mandatory", name);
  return get_texture(desc, name);
}

std::optional<JbSkyboxExtData> jb_skybox_parse_desc(const tinygltf::Model& model)
{
  if (!model.extensions.contains("JB_skybox"))
    return std::nullopt;

  const auto& desc = model.extensions.at("JB_skybox");

  JbSkyboxExtData data{};
  data.cubemap = get_mandatory_texture(desc, "cubemap");
  return data;
}
