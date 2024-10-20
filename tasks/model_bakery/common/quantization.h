#ifndef QUANTIZATION_H_INCLUDED
#define QUANTIZATION_H_INCLUDED

#ifdef __cplusplus

#include <glm/glm.hpp>

inline int64_t quantize3f6b2f2b(glm::vec3 in3, glm::vec2 in2)
{
  int64_t x2 = (int8_t)(in2.x * 127.0);
  int64_t y2 = (int8_t)(in2.y * 127.0);
  int64_t x3 = (int16_t)(in3.x * 32767.0);
  int64_t y3 = (int16_t)(in3.y * 32767.0);
  int64_t z3 = (int16_t)(in3.z * 32767.0);
  return (x2 << 56) | (y2 << 48) | (x3 << 32) | (y3 << 16) | z3;
}

#else

float dequantizef16(uint val)
{
  float sgn = 1.0;
  if ((val & 0x8000) != 0)
  {
    val = 0x10000 - val;
    sgn = -1.0;
  }
  return sgn * float(val) / 32767.0;
}

float dequantizef8(uint val)
{
  float sgn = 1.0;
  if ((val & 0x80) != 0)
  {
    val = 0x100 - val;
    sgn = -1.0;
  }
  return sgn * float(val) / 127.0;
}
 
void dequantize3f6b2f2b(uint lo, uint hi, out vec3 v3, out vec2 v2)
{
  uint xq2 = (hi >> 24) & 0xFF;
  uint yq2 = (hi >> 16) & 0xFF;
  uint xq3 = hi & 0xFFFF;
  uint yq3 = (lo >> 16) & 0xFFFF;
  uint zq3 = lo & 0xFFFF;

  v2 = vec2(dequantizef8(xq2), dequantizef8(yq2)); 
  v3 = vec3(dequantizef16(xq3), dequantizef16(yq3), dequantizef16(zq3)); 
}

#endif

#endif // QUANTIZATION_H_INCLUDED
