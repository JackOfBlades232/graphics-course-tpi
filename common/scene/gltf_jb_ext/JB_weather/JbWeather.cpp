#include "JbWeather.hpp"

#include <spdlog/spdlog.h>

// @TODO: pull out
#define LOG(fmt_, ...) spdlog::info("[JB_weather]: " fmt_ __VA_OPT__(, ) __VA_ARGS__)
#define FAIL(fmt_, ...)                                                                            \
  do                                                                                               \
  {                                                                                                \
    spdlog::error("[JB_weather]: " fmt_ __VA_OPT__(, ) __VA_ARGS__);                               \
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

static Attribute<const tinygltf::Value*> get_subobject(const tinygltf::Value& obj, const char* name)
{
  if (obj.Has(name))
    return {&obj.Get(name), {}};
  else
    return {nullptr, fmt::format("Missing attribute \"{}\"", name)};
}

static Attribute<glm::vec2> get_vec2(const tinygltf::Value& obj, const char* name)
{
  auto attr = get_subobject(obj, name);
  if (const auto* vec = attr.value)
  {
    if (!vec->IsArray() || vec->ArrayLen() != 2)
      return {{}, fmt::format("\"{}\" must be a 2d array", name)};
    glm::vec2 v{};
    float* p = (float*)&v;
    for (int i = 0; i < 2; ++i, ++p)
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

template <class T>
static T unpack(Attribute<T>&& attr)
{
  if (!attr.error.empty())
    FAIL("{}", attr.error);
  return std::move(attr.value);
}

std::optional<JbWeatherExtData> jb_weather_parse_desc(const tinygltf::Model& model)
{
  if (!model.extensions.contains("JB_weather"))
    return std::nullopt;

  const auto& desc = model.extensions.at("JB_weather");

  JbWeatherExtData data{};

#define FETCHM(obj_, name_, getter_) auto name_ = unpack(getter_((obj_), #name_));

  auto wattr = get_subobject(desc, "wind");
  if (const auto* w = wattr.value)
  {
    FETCHM(*w, direction, get_vec2);
    FETCHM(*w, strength, get_float);
    if (glm::length(direction) < FLT_EPSILON)
      FAIL("wind/direction can not be a zero vector");
    data.wind.direction = glm::normalize(direction);
    data.wind.strength = strength;
  }
  else
  {
    FAIL("{}", wattr.error);
  }

  auto fattr = get_subobject(desc, "fog");
  if (const auto* f = fattr.value)
  {
    FETCHM(*f, rho0, get_float);
    if (rho0 < 0.f)
    {
      FAIL("fog/rho0 must not be negative");
    }
    else if (rho0 > FLT_EPSILON)
    {
      data.fog.rho0 = rho0;
      FETCHM(*f, hBase, get_float);
      FETCHM(*f, hC, get_float);
      if (hC < 0.f)
        FAIL("fog/hC must not be negative");
      data.fog.hBase = hBase;
      data.fog.hC = hC;
      auto sattr = get_subobject(*f, "shape");
      if (const auto* s = sattr.value)
      {
        FETCHM(*s, rngMin, get_float);
        FETCHM(*s, rngMax, get_float);
        FETCHM(*s, scale, get_float);
        FETCHM(*s, windInfluence, get_float);
        FETCHM(*s, fadeoutStart, get_float);
        FETCHM(*s, fadeoutSize, get_float);
        if (rngMin < 0.f || rngMin > 1.f || rngMax < 0.f || rngMax > 1.f || rngMin >= rngMax)
          FAIL("fog/shape/rngMin and rngMax must be in [0, 1] and min must be less than max");
        if (scale < FLT_EPSILON)
          FAIL("fog/shape/scale must be positive");
        if (windInfluence < 0.f)
          FAIL("fog/shape/windInfluence must not be negative");
        if (fadeoutStart < 0.f)
          FAIL("fog/shape/fadeoutStart must not be negative");
        if (fadeoutSize < FLT_EPSILON)
          FAIL("fog/shape/fadeoutSize must be positive");
        data.fog.shape.rngMin = rngMin;
        data.fog.shape.rngMax = rngMax;
        data.fog.shape.scale = scale;
        data.fog.shape.windInfluence = windInfluence;
        data.fog.shape.fadeoutStart = fadeoutStart;
        data.fog.shape.fadeoutSize = fadeoutSize;
      }
      auto iattr = get_subobject(*f, "inscatter");
      if (const auto* i = iattr.value)
      {
        FETCHM(*i, c, get_float);
        if (c < 0.f)
          FAIL("fog/inscatter/c must not be negative");
        data.fog.inscatter.c = c;
      }
    }
  }

  return data;
}
