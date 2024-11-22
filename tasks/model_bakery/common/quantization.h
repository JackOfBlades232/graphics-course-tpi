#ifndef QUANTIZATION_H_INCLUDED
#define QUANTIZATION_H_INCLUDED

#include <bit>
#ifdef __cplusplus

#include <glm/glm.hpp>

inline uint32_t quantize4fnorm(glm::vec4 in)
{
  int8_t coords[4] = {
    (int8_t)roundf(in.x * 127.f),
    (int8_t)roundf(in.y * 127.f),
    (int8_t)roundf(in.z * 127.f),
    (int8_t)roundf(in.w * 127.f)};
  return std::bit_cast<uint32_t>(coords);
}

#else
 
vec3 dequantize3fnorm(uint q)
{
  const uint a_enc_x = (q  & 0x000000FFu);
  const uint a_enc_y = ((q & 0x0000FF00u) >> 8);
  const uint a_enc_z = ((q & 0x00FF0000u) >> 16);

  const uint usX = uint(a_enc_x & 0x000000FFu);
  const uint usY = uint(a_enc_y & 0x000000FFu);
  const uint usZ = uint(a_enc_z & 0x000000FFu);

  const int sX = (usX <= 127) ? usX : (int(usX) - 256);
  const int sY = (usY <= 127) ? usY : (int(usY) - 256);
  const int sZ = (usZ <= 127) ? usZ : (int(usZ) - 256);

  const float x = max(float(sX) / 127.0f, -1.0);
  const float y = max(float(sY) / 127.0f, -1.0);
  const float z = max(float(sZ) / 127.0f, -1.0);

  return vec3(x, y, z);
}

#endif

#endif // QUANTIZATION_H_INCLUDED
