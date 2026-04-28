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
layout(binding = 9, set = 0) uniform view_params_t
{
  ViewParams viewParams;
};
layout(binding = 10, set = 0) readonly buffer view_data_t
{
  ViewData viewData;
};

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

void main()
{
  vec2 texelSize = 1.f / vec2(textureSize(aoInput, 0));
  float centerDepth = texture(aoInput, surf.texCoord).y;
  float result = 0.f;
  float tw = 0.f;
  for (int x = -SSAO_BLUR_KERNEL_HS; x <= SSAO_BLUR_KERNEL_HS; ++x) 
  {
    for (int y = -SSAO_BLUR_KERNEL_HS; y <= SSAO_BLUR_KERNEL_HS; ++y) 
    {
      vec2 off = vec2(float(x), float(y)) * texelSize;
      vec2 aoz = texture(aoInput, surf.texCoord + off).xy;
      float ao = aoz.x;
      float d = aoz.y;
      float dd = abs(centerDepth - d);
      float dw = max(0.f, 1.f - dd / (2.f * constants.ssaoRadius)); // 0 when kernels do not intersect
      result += ao * dw;
      tw += dw;
    }
  }
  out_ao = result / tw;
  if (abs(constants.ssaoPower - 1.f) > SHADER_EPSILON)
    out_ao = pow(out_ao, constants.ssaoPower);
}

