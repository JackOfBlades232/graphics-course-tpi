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

template <class T>
struct Attribute
{
  T value;
  std::string error;
};

#define MANDATORY(attr_)                                                                           \
  ([&]() {                                                                                         \
    auto tmp = (attr_);                                                                            \
    if (!tmp.error.empty())                                                                        \
      FAIL("invalid format: {}", tmp.error);                                                       \
    return tmp.value;                                                                              \
  }())

static Attribute<const tinygltf::Value*> get_subobject(const tinygltf::Value& obj, const char* name)
{
  if (obj.Has(name))
    return {&obj.Get(name), {}};
  else
    return {nullptr, fmt::format("Missing attribute \"{}\"", name)};
}

static Attribute<glm::vec3> get_vec3(const tinygltf::Value& obj, const char* name)
{
  auto attr = get_subobject(obj, name);
  if (const auto* vec = attr.value)
  {
    if (!vec->IsArray() || vec->ArrayLen() != 3)
      return {{}, fmt::format("\"{}\" must be a 3d array", name)};
    glm::vec3 v{};
    float* p = (float*)&v;
    for (int i = 0; i < 3; ++i, ++p)
    {
      const auto& elem = vec->Get(i);
      if (!elem.IsNumber())
        return {{}, fmt::format("\"{}\" must be a vector of doubles", name)};
      *p = float(elem.GetNumberAsDouble());
    }
    return {v, {}};
  }
  else
  {
    return {{}, std::move(attr.error)};
  }
}

static Attribute<float> get_float(const tinygltf::Value& obj, const char* name)
{
  auto attr = get_subobject(obj, name);
  if (const auto* num = attr.value)
  {
    if (!num->IsNumber())
      return {{}, fmt::format("\"{}\" must be a double", name)};
    return {float(num->GetNumberAsDouble()), {}};
  }
  else
  {
    return {{}, std::move(attr.error)};
  }
}

static Attribute<JbWaterExtSpectrum> get_spectrum(const tinygltf::Value& obj)
{
  auto conf = get_subobject(obj, "spectrum");
  if (const auto* spc = conf.value)
  {
    JbWaterExtSpectrum sp{};
    for (size_t offset = 0;
         const char* name :
         {"shallowCutoff", "depth", "fetch", "scale", "gamma", "cascadeCutoffScale"})
    {
      auto attr = get_float(*spc, name);
      if (!attr.error.empty())
        return {{}, "spectrum: " + attr.error};
      ((float*)&sp)[offset++] = attr.value;
    }
    return {sp, {}};
  }
  else
  {
    return {{}, "missing \"spectrum\" block"};
  }
}

static Attribute<JbWaterExtLightingCaustics> get_caustics(const tinygltf::Value& obj)
{
  auto conf = get_subobject(obj, "caustics");
  if (const auto* spc = conf.value)
  {
    JbWaterExtLightingCaustics sp{};
    for (size_t offset = 0; const char* name :
                            {"projectionFloorDepth",
                             "tilelWorldSize",
                             "tileApronUvSize",
                             "lightScale",
                             "renderDistance",
                             "renderFadeout"})
    {
      auto attr = get_float(*spc, name);
      if (!attr.error.empty())
        return {{}, "caustics: " + attr.error};
      ((float*)&sp)[offset++] = attr.value;
    }
    return {sp, {}};
  }
  else
  {
    return {{}, "missing \"caustics\" block"};
  }
}

static Attribute<JbWaterExtLightingRefraction> get_refraction(const tinygltf::Value& obj)
{
  auto conf = get_subobject(obj, "refraction");
  if (const auto* spc = conf.value)
  {
    JbWaterExtLightingRefraction sp{};
    {
      auto col = get_vec3(*spc, "color");
      if (!col.error.empty())
        return {{}, "refraction: " + col.error};
      sp.color = col.value;
    }
    for (size_t offset = 0; const char* name : {"depth", "screen"})
    {
      auto attr = get_float(*spc, name);
      if (!attr.error.empty())
        return {{}, "refraction: " + attr.error};
      ((float*)&sp.depth)[offset++] = attr.value;
    }
    if (spc->Has("caustics"))
      sp.caustics = MANDATORY(get_caustics(*spc));
    return {sp, {}};
  }
  else
  {
    return {{}, "missing \"refraction\" block"};
  }
}

static Attribute<JbWaterExtLightingFoam> get_foam(const tinygltf::Value& obj)
{
  auto conf = get_subobject(obj, "foam");
  if (const auto* spc = conf.value)
  {
    JbWaterExtLightingFoam sp{};
    {
      auto col = get_vec3(*spc, "color");
      if (!col.error.empty())
        return {{}, "foam: " + col.error};
      sp.color = col.value;
    }
    for (size_t offset = 0;
         const char* name : {"roughness", "turbulenceBaseline", "turbulenceFadeout"})
    {
      auto attr = get_float(*spc, name);
      if (!attr.error.empty())
        return {{}, "foam: " + attr.error};
      ((float*)&sp.roughness)[offset++] = attr.value;
    }
    return {sp, {}};
  }
  else
  {
    return {{}, "missing \"foam\" block"};
  }
}

static Attribute<JbWaterExtLighting> get_lighting(const tinygltf::Value& obj)
{
  auto conf = get_subobject(obj, "lighting");
  if (const auto* light = conf.value)
  {
    JbWaterExtLighting lighting{};
    {
      auto col = get_vec3(*light, "surfaceColor");
      if (!col.error.empty())
        return {{}, "lighting: " + col.error};
      lighting.surfaceColor = col.value;
    }
    if (light->Has("refraction"))
      lighting.refraction = MANDATORY(get_refraction(*light));
    if (light->Has("foam"))
      lighting.foam = MANDATORY(get_foam(*light));
    return {lighting, {}};
  }
  else
  {
    return {{}, "missing \"lighting\" block"};
  }
}

static Attribute<JbWaterExtShore> get_shore(const tinygltf::Value& obj)
{
  auto conf = get_subobject(obj, "shore");
  if (const auto* spc = conf.value)
  {
    JbWaterExtShore sp{};
    for (size_t offset = 0; const char* name :
                            {"depth",
                             "shallowPow",
                             "steepMaxCoeff",
                             "steepWeight",
                             "permanentFoamWeight",
                             "permanentFoamDepth",
                             "permanentFoamF",
                             "cyclicFoamWeight",
                             "cyclicFoamDepth",
                             "cyclicFoamF",
                             "cyclicFoamSpeed"})
    {
      auto attr = get_float(*spc, name);
      if (!attr.error.empty())
        return {{}, "shore: " + attr.error};
      ((float*)&sp)[offset++] = attr.value;
    }
    return {sp, {}};
  }
  else
  {
    return {{}, "missing \"shore\" block"};
  }
}

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

  data.spectrum = MANDATORY(get_spectrum(desc));
  data.lighting = MANDATORY(get_lighting(desc));
  if (desc.Has("shore"))
    data.shore = MANDATORY(get_shore(desc));

  return data;
}
