#ifndef QUANTIZATION_H_INCLUDED
#define QUANTIZATION_H_INCLUDED

#ifdef __cplusplus

#include <glm/glm.hpp>

inline uint32_t quantize3fnorm(glm::vec3 in)
{
  int32_t x = (int32_t)(in.x * 127.0) & 0xFF;
  int32_t y = (int32_t)(in.y * 127.0) & 0xFF;
  int32_t z = (int32_t)(in.z * 127.0) & 0xFF;
  return (z << 16) | (y << 8) | x;
}

#else
 
vec3 dequantize3fnorm(uint q)
{
  const uint a_enc_x = (q  & 0x000000FFu);
  const uint a_enc_y = ((q & 0x0000FF00u) >> 8);
  const uint a_enc_z = ((q & 0x00FF0000u) >> 16);

  const int usX = int(a_enc_x & 0x000000FFu);
  const int usY = int(a_enc_y & 0x000000FFu);
  const int usZ = int(a_enc_z & 0x000000FFu);

  const int sX = (usX <= 127) ? usX : usX - 256;
  const int sY = (usY <= 127) ? usY : usY - 256;
  const int sZ = (usZ <= 127) ? usZ : usZ - 256;

  const float x = sX*(1.0f / 127.0f);
  const float y = sY*(1.0f / 127.0f);
  const float z = sZ*(1.0f / 127.0f);

  return vec3(x, y, z);
}

#endif

#endif // QUANTIZATION_H_INCLUDED
