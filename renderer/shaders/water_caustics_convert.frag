#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "water.h"

layout(location = 0) out vec4 color;

layout(binding = 0, set = 0) readonly buffer caustics_t
{
  uint accum[];
};

layout(location = 0) in VS_OUT
{
  vec2 texCoord;
} surf;

const float gaussian_r1[3] = float[](0.27406862, 0.45186276, 0.27406862);
const float gaussian_r2[5] = float[](0.05448868, 0.24420134, 0.40261995, 0.24420134, 0.05448868);
const float gaussian_r4[9] = float[](
  0.01621617,
  0.05400558,
  0.12164907,
  0.19321346,
  0.22983144,
  0.19321346,
  0.12164907,
  0.05400558,
  0.01621617);
const float gaussian_r8[17] = float[](
  0.00442991,
  0.00895781,
  0.01653865,
  0.02783468,
  0.04315812,
  0.06184217,
  0.08193827,
  0.09965735,
  0.10641118,
  0.09965735,
  0.08193827,
  0.06184217,
  0.04315812,
  0.02783468,
  0.01653865,
  0.00895781,
  0.00442991);

void main()
{
  ivec2 imCoord = ivec2(surf.texCoord * float(WATER_CAUSTIC_MAP_RES));
  float total = 0.f;
  float tw = 0.f;
  for (int y = -2; y <= 2; ++y)
    for (int x = -2; x <= 2; ++x)
    {
      float w = gaussian_r2[x + 2] * gaussian_r2[y + 2];
      ivec2 rc = clamp(imCoord + ivec2(x, y), ivec2(0), ivec2(WATER_CAUSTIC_MAP_RES - 1));
      float val = float(accum[rc.x + WATER_CAUSTIC_MAP_RES * rc.y]) / 255.f;
      total += val * w;
      tw += w;
    }
  float colorgrade = total / tw;
  color = vec4(colorgrade, colorgrade, colorgrade, 1.f);
}

