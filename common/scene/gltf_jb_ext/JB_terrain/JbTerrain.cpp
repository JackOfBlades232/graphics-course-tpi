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
  data.diffuse = get_opt_texture(desc, "diffuse");
  data.errosion = get_opt_texture(desc, "errosion");

  if (desc.Has("errosionSeed"))
  {
    const auto& eseed = desc.Get("errosionSeed");
    VERIFY(eseed.IsNumber(), "invalid format: \"errosionSeed\" must be an int");
    data.errosionSeed = eseed.GetNumberAsInt();
  }

  if (desc.Has("heightmapRange"))
  {
    const auto& hrange = desc.Get("heightmapRange");
#define HMAP_RNG_FORMAT_ERR "invalid format: \"heightmapRange\" must be an array of two doubles"
    VERIFY(hrange.IsArray() && hrange.ArrayLen() == 2, HMAP_RNG_FORMAT_ERR);

    for (int i = 0; i < 2; ++i)
    {
      const auto& elem = hrange.Get(i);
      VERIFY(elem.IsNumber(), HMAP_RNG_FORMAT_ERR);
      data.heightmapRange[i] = elem.GetNumberAsDouble();
    }
#undef HMAP_RNG_FORMAT_ERR
  }

  return data;
}
