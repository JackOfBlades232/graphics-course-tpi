#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "constants.h"

layout(location = 0) out vec4 out_fragColor;

layout(binding = 0) uniform sampler2D ldrImage;

layout(binding = 1) uniform sampler2D prevFrame;
layout(binding = 2) uniform sampler2D motionVectors;
layout(binding = 3) uniform sampler2D gbufDepth;

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

#include "tonemap.frag.inc"

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

void main(void)
{
  const vec3 currentCol = textureLod(ldrImage, surf.texCoord, 0.f).xyz;

  // @TEST iter 1: let there be smear!
  const vec3 prevCol = textureLod(prevFrame, surf.texCoord, 0.f).xyz;
  const vec3 finalCol = mix(prevCol, currentCol, constants.taaEmaCoeff);

  // No gamma encoding here -- we need linear for history
  out_fragColor = vec4(finalCol, 1.f);
}

