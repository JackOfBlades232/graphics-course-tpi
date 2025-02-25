#ifndef QUANTIZATION_H_INCLUDED
#define QUANTIZATION_H_INCLUDED

#ifdef __cplusplus

#include <glm/glm.hpp>
#include <algorithm>
#include <bit>

inline uint32_t quantize4fnorm(glm::vec4 in)
{
  int8_t coords[4] = {
    (int8_t)roundf(in.x * 127.f),
    (int8_t)roundf(in.y * 127.f),
    (int8_t)roundf(in.z * 127.f),
    (int8_t)roundf(in.w * 127.f)};
  return std::bit_cast<uint32_t>(coords);
}

glm::vec3 dequantize3fnorm(uint32_t q)
{
  const uint32_t aEncX = (q & 0x000000FFu);
  const uint32_t aEncY = ((q & 0x0000FF00u) >> 8);
  const uint32_t aEncZ = ((q & 0x00FF0000u) >> 16);

  const uint32_t usX = uint32_t(aEncX & 0x000000FFu);
  const uint32_t usY = uint32_t(aEncY & 0x000000FFu);
  const uint32_t usZ = uint32_t(aEncZ & 0x000000FFu);

  const int32_t sX = (usX <= 127) ? usX : (int32_t(usX) - 256);
  const int32_t sY = (usY <= 127) ? usY : (int32_t(usY) - 256);
  const int32_t sZ = (usZ <= 127) ? usZ : (int32_t(usZ) - 256);

  const float x = std::max(float(sX) / 127.0f, -1.0f);
  const float y = std::max(float(sY) / 127.0f, -1.0f);
  const float z = std::max(float(sZ) / 127.0f, -1.0f);

  return glm::vec3{x, y, z};
}

glm::vec4 dequantize3f1snorm(uint32_t q)
{
  const uint32_t aEncX = (q & 0x000000FFu);
  const uint32_t aEncY = ((q & 0x0000FF00u) >> 8);
  const uint32_t aEncZ = ((q & 0x00FF0000u) >> 16);
  const uint32_t aEncOri = ((q & 0xFF000000u) >> 24);

  const uint32_t usX = uint32_t(aEncX & 0x000000FFu);
  const uint32_t usY = uint32_t(aEncY & 0x000000FFu);
  const uint32_t usZ = uint32_t(aEncZ & 0x000000FFu);
  const uint32_t usOri = uint32_t(aEncOri & 0x000000FFu);

  const int32_t sX = (usX <= 127) ? usX : (int32_t(usX) - 256);
  const int32_t sY = (usY <= 127) ? usY : (int32_t(usY) - 256);
  const int32_t sZ = (usZ <= 127) ? usZ : (int32_t(usZ) - 256);
  const int32_t sOri = (usOri <= 127) ? usOri : (int32_t(usOri) - 256);

  const float x = std::max(float(sX) / 127.f, -1.f);
  const float y = std::max(float(sY) / 127.f, -1.f);
  const float z = std::max(float(sZ) / 127.f, -1.f);
  const float ori = sOri >= 0.f ? 1.f : -1.f;

  return glm::vec4{x, y, z, ori};
}

uint32_t quantizefcol(float r)
{
  auto ur = uint32_t(r * 255.f) & 0xFF;
  return (ur << 24) | (ur << 16) | (ur << 8) | ur;
}

uint32_t quantize4fcol(glm::vec4 c)
{
  auto ur = uint32_t(c.r * 255.f) & 0xFF;
  auto ug = uint32_t(c.g * 255.f) & 0xFF;
  auto ub = uint32_t(c.b * 255.f) & 0xFF;
  auto ua = uint32_t(c.a * 255.f) & 0xFF;
  return (ua << 24) | (ub << 16) | (ug << 8) | ur;
}

#else

vec3 dequantize3fnorm(uint q)
{
  const uint aEncX = (q & 0x000000FFu);
  const uint aEncY = ((q & 0x0000FF00u) >> 8);
  const uint aEncZ = ((q & 0x00FF0000u) >> 16);

  const uint usX = uint(aEncX & 0x000000FFu);
  const uint usY = uint(aEncY & 0x000000FFu);
  const uint usZ = uint(aEncZ & 0x000000FFu);

  const int sX = (usX <= 127) ? int(usX) : (int(usX) - 256);
  const int sY = (usY <= 127) ? int(usY) : (int(usY) - 256);
  const int sZ = (usZ <= 127) ? int(usZ) : (int(usZ) - 256);

  const float x = max(float(sX) / 127.0f, -1.0f);
  const float y = max(float(sY) / 127.0f, -1.0f);
  const float z = max(float(sZ) / 127.0f, -1.0f);

  return vec3(x, y, z);
}

vec4 dequantize3f1snorm(uint q)
{
  const uint aEncX = (q & 0x000000FFu);
  const uint aEncY = ((q & 0x0000FF00u) >> 8);
  const uint aEncZ = ((q & 0x00FF0000u) >> 16);
  const uint aEncOri = ((q & 0xFF000000u) >> 24);

  const uint usX = uint(aEncX & 0x000000FFu);
  const uint usY = uint(aEncY & 0x000000FFu);
  const uint usZ = uint(aEncZ & 0x000000FFu);
  const uint usOri = uint(aEncOri & 0x000000FFu);

  const int sX = (usX <= 127) ? int(usX) : (int(usX) - 256);
  const int sY = (usY <= 127) ? int(usY) : (int(usY) - 256);
  const int sZ = (usZ <= 127) ? int(usZ) : (int(usZ) - 256);
  const int sOri = (usOri <= 127) ? int(usOri) : (int(usOri) - 256);

  const float x = max(float(sX) / 127.0f, -1.0f);
  const float y = max(float(sY) / 127.0f, -1.0f);
  const float z = max(float(sZ) / 127.0f, -1.0f);
  const float ori = sOri >= 0.0f ? 1.0f : -1.0f;

  return vec4(x, y, z, ori);
}

vec4 dequantize4fcol(uint c)
{
  const uint aEncR = (c & 0x000000FFu);
  const uint aEncG = ((c & 0x0000FF00u) >> 8);
  const uint aEncB = ((c & 0x00FF0000u) >> 16);
  const uint aEncA = ((c & 0xFF000000u) >> 24);
  return vec4(
    float(aEncR) / 255.0f,
    float(aEncG) / 255.0f,
    float(aEncB) / 255.0f,
    float(aEncA) / 255.0f);
}

#endif

#endif // QUANTIZATION_H_INCLUDED
