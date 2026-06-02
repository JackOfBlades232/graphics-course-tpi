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
  vec3 col = textureLod(ldrImage, surf.texCoord, 0.f).xyz;

  // @TEST iter 2: reproject but dont reject
  if (constants.frameNo > 0)
  {
    vec2 motion = textureLod(motionVectors, surf.texCoord, 0).xy;
    vec2 uv = surf.texCoord - motion;
    if (uv.x >= 0.f && uv.x <= 1.f && uv.y >= 0.f && uv.y <= 1.f)
    {
      const vec3 prevCol = textureLod(prevFrame, uv, 0).xyz;
      col = mix(prevCol, col, constants.taaEmaCoeff);
    }
  }

  // No gamma encoding here -- we need linear for history
  out_fragColor = vec4(col, 1.f);
}

