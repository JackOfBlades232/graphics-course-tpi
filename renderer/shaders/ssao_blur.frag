#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : enable

#include "ssao.h"
#include "constants.h"


layout(location = 0) out float out_ao;

layout(binding = 0, set = 0) uniform sampler2D aoInput;

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

void main()
{
  vec2 texelSize = 1.f / vec2(textureSize(aoInput, 0));
  float result = 0.f;
  for (int x = -2; x < 2; ++x) 
  {
    for (int y = -2; y < 2; ++y) 
    {
      vec2 off = vec2(float(x), float(y)) * texelSize;
      result += texture(aoInput, surf.texCoord + off).x;
    }
  }
  out_ao = result / (4.f * 4.f);
}

