#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "water.h"

layout(location = 0) out vec4 color;

layout(binding = 0, set = 0) readonly buffer caustics_t
{
  uint accum[];
};

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

void main()
{
  ivec2 imCoord = ivec2(surf.texCoord * float(WATER_CAUSTIC_MAP_RES));
  uint value = accum[imCoord.x + WATER_CAUSTIC_MAP_RES * imCoord.y];
  float colorgrade = float(value) / 255.f;
  color = vec4(colorgrade, colorgrade, colorgrade, 1.f);
}

