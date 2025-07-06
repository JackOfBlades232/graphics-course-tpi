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
  data.splattingMask = get_opt_texture(desc, "splattingMaskTexture");

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

  if (desc.Has("details"))
  {
    const auto& dets = desc.Get("details");
    VERIFY(dets.IsArray(), "invalid format: \"details\" must be an array");
    for (size_t i = 0; i < dets.ArrayLen(); ++i)
    {
      const auto& elem = dets.Get(i);
      VERIFY(elem.IsObject(), "invalid format: \"details\" element is not an object");

      auto& dst = data.details.emplace_back();

      {
        VERIFY(elem.Has("material"), "invalid format: detail must have a \"material\" field");
        const auto& mat = elem.Get("material");
        VERIFY(
          mat.IsNumber() && mat.GetNumberAsInt() >= 0,
          "invalid format: detail \"material\" field must be a non-negative integer "
          "index");
        dst.material = mat.GetNumberAsInt();
      }

      if (elem.Has("name"))
      {
        const auto& name = elem.Get("name");
        VERIFY(name.IsString(), "invalid format: detail \"name\" must be a string");
        dst.name = name.Get<std::string>();
      }

      if (elem.Has("scale"))
      {
#define SCALE_RNG_FORMAT_ERR                                                                       \
  "invalid format: detail \"scale\" must be a 2-element double array in [0, inf] range"
        const auto& sc = elem.Get("scale");
        VERIFY(sc.IsArray() && sc.ArrayLen() == 2, SCALE_RNG_FORMAT_ERR);

        const auto& x = sc.Get(0);
        const auto& y = sc.Get(1);
        VERIFY(x.IsNumber() && y.IsNumber(), SCALE_RNG_FORMAT_ERR);

        const double xv = x.GetNumberAsDouble();
        const double yv = y.GetNumberAsDouble();
        VERIFY(xv >= 0.0 && yv >= 0.0, SCALE_RNG_FORMAT_ERR);

        dst.uvScale = {xv, yv};
#undef SCALE_RNG_FORMAT_ERR
      }

      if (elem.Has("relHeightRange"))
      {
#define REL_H_RNG_FORMAT_ERR                                                                       \
  "invalid format: detail \"relHeightRange\" must be a 2-element double array, with [0] < [1] "    \
  "and both in [0, 1] range"
        const auto& rng = elem.Get("relHeightRange");
        VERIFY(rng.IsArray() && rng.ArrayLen() == 2, REL_H_RNG_FORMAT_ERR);

        const auto& x = rng.Get(0);
        const auto& y = rng.Get(1);
        VERIFY(x.IsNumber() && y.IsNumber(), REL_H_RNG_FORMAT_ERR);

        const double xv = x.GetNumberAsDouble();
        const double yv = y.GetNumberAsDouble();
        VERIFY(
          (xv >= 0.0 && xv <= 1.0) && (yv >= 0.0 && yv <= 1.0) && xv < yv, REL_H_RNG_FORMAT_ERR);

        dst.relHeightRange = {xv, yv};
        dst.useRelHeightRange = true;
#undef REL_H_RNG_FORMAT_ERR
      }

      // @TODO: syntax for splatting mask components mapping
    }
  }

  return data;
}

std::optional<JbTerrainExtMaterial> jb_terrain_parse_material_desc(const tinygltf::Material& mat)
{
  if (!mat.extensions.contains("JB_terrain"))
    return std::nullopt;

  const auto& desc = mat.extensions.at("JB_terrain");

  JbTerrainExtMaterial data{};

  data.displacement = get_mandatory_texture(desc, "displacementTexture");
  return data;
}
