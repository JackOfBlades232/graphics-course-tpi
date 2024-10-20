#ifndef OCTAHEDRAL_H_INCLUDED
#define OCTAHEDRAL_H_INCLUDED

#ifdef __cplusplus

#include <glm/glm.hpp>

inline glm::vec2 oct_wrap(glm::vec2 v)
{
  return (glm::vec2(1.f) - glm::abs(glm::vec2(v.y, v.x))) *
    glm::vec2(v.x >= 0.f ? 1.f : -1.f, v.y >= 0.f ? 1.f : -1.f);
}

inline glm::vec2 oct_encode2(glm::vec3 n)
{
  n /= (glm::abs(n.x) + glm::abs(n.y) + glm::abs(n.z));
  glm::vec2 v = n.z >= 0.f ? glm::vec2(n.x, n.y) : oct_wrap(glm::vec2(n.x, n.y));
  return v * glm::vec2(0.5f) + glm::vec2(0.5f);
}

inline uint32_t oct_encode(glm::vec3 v)
{
  glm::vec2 oct = oct_encode2(v);
  const uint32_t x = (int16_t)(oct.x * 32767.f);
  const uint32_t y = (int16_t)(oct.y * 32767.f);
  return (x << 16) | y;
}

#else
 
vec3 oct_decode2(vec2 f)
{
    f = f * 2.0 - 1.0;
 
    // https://twitter.com/Stubbesaurus/status/937994790553227264
    vec3 n = vec3(f.x, f.y, 1.0 - abs(f.x) - abs(f.y));
    float t = clamp(-n.z, 0.0, 1.0);
    n.xy += vec2(n.x >= 0.0 ? -t : t, n.y >= 0.0 ? -t : t);
    return normalize(n);
}

vec3 oct_decode(uint v)
{
  float x = float(v >> 16) / 32767.0;
  float y = float(v & 0xFFFF) / 32767.0;
  return oct_decode2(vec2(x, y));
}

#endif

#endif // OCTAHEDRAL_H_INCLUDED
