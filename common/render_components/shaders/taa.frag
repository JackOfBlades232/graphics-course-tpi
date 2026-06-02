#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "constants.h"

layout(location = 0) out vec4 out_fragColor;

layout(binding = 0) uniform sampler2D ldrImage;

layout(binding = 1) uniform sampler2D prevFrame;
layout(binding = 2) uniform sampler2D motionVectors;
layout(binding = 3) uniform sampler2D prevMotionVectors;

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

#include "tonemap.frag.inc"

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

const float GAUSS_KERNEL[3][3] = {
  {0.0625, 0.125, 0.0625},
  {0.125,  0.25,  0.125},
  {0.0625, 0.125, 0.0625}
};

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
      vec3 prevCol = textureLod(prevFrame, uv, 0).xyz;

      vec3 minNeiCol = vec3(9999.f);
      vec3 maxNeiCol = vec3(-9999.f);
      vec3 blurredNeiCol = vec3(0.f);
      for (int x = -1; x <= 1; ++x)
        for (int y = -1; y <= 1; ++y)
        {
          vec2 neiUv = surf.texCoord + vec2(x, y) * constants.mainTargetInverseResolution;
          neiUv = clamp(neiUv, 0.f, 1.f);
          vec3 nc = textureLod(ldrImage, neiUv, 0).xyz;
          minNeiCol = min(minNeiCol, nc);
          maxNeiCol = max(maxNeiCol, nc);
          blurredNeiCol += nc * GAUSS_KERNEL[x + 1][y + 1];
        }

      prevCol = clamp(prevCol, minNeiCol, maxNeiCol);

      vec3 accum = mix(prevCol, col, constants.taaEmaCoeff);

      vec2 prevMotion = textureLod(prevMotionVectors, uv, 0).xy;
      float motionChangeLength = length(motion - prevMotion);

      float velocityDisocclusion = clamp((motionChangeLength - 0.001f) * 10.f, 0.f, 1.f);

      col = mix(accum, blurredNeiCol, velocityDisocclusion);

      // @TEST
      col = vec3(velocityDisocclusion);
    }
  }

  // No gamma encoding here -- we need linear for history
  out_fragColor = vec4(col, 1.f);
}

