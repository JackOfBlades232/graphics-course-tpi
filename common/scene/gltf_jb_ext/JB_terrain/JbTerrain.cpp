#include "JbTerrain.hpp"

#include <spdlog/spdlog.h>

// @TODO: pull out
#define LOG(fmt_, ...) spdlog::info("[JB_terrain]: " fmt_ __VA_OPT__(, ) __VA_ARGS__)
#define FAIL(fmt_, ...)                                                                            \
  do                                                                                               \
  {                                                                                                \
    spdlog::error("[JB_terrain]: " fmt_ __VA_OPT__(, ) __VA_ARGS__);                               \
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
static int get_opt_texture(const auto& desc, const char* name)
{
  if (desc.Has(name))
    return get_texture(desc, name);
  return -1;
}

std::optional<JbTerrainExtData> jb_terrain_parse_desc(const tinygltf::Model& model)
{
  if (!model.extensions.contains("JB_terrain"))
    return std::nullopt;

  const auto& desc = model.extensions.at("JB_terrain");

  JbTerrainExtData data{};

  data.heightmap = get_mandatory_texture(desc, "heightmap");

  if (desc.Has("range"))
  {
    const auto& hrange = desc.Get("range");
#define HMAP_RNG_FORMAT_ERR "invalid format: \"range\" must be an array of 6 doubles"
    VERIFY(hrange.IsArray() && hrange.ArrayLen() == 6, HMAP_RNG_FORMAT_ERR);

    float* dst[] = {
      &data.rangeMin.x,
      &data.rangeMax.x,
      &data.rangeMin.y,
      &data.rangeMax.y,
      &data.rangeMin.z,
      &data.rangeMax.z};

    for (int i = 0; i < 6; ++i)
    {
      const auto& elem = hrange.Get(i);
      VERIFY(elem.IsNumber(), HMAP_RNG_FORMAT_ERR);
      *dst[i] = float(elem.GetNumberAsDouble());
    }
#undef HMAP_RNG_FORMAT_ERR
  }

  if (desc.Has("noiseSeed"))
  {
    const auto& seed = desc.Get("noiseSeed");
    VERIFY(seed.IsNumber(), "invalid format: \"noiseSeed\" must be an integer");

    data.noiseSeed = seed.GetNumberAsInt();
  }

  return data;
}
