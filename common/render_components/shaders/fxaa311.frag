#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "constants.h"

layout(location = 0) out vec4 out_fragColor;

layout(binding = 0) uniform sampler2D ldrImage;

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

#include "fxaa_common.frag.inc"

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

void main(void)
{
  const vec3 centerCol = textureLod(ldrImage, surf.texCoord, 0.f).xyz;

  // @TODO: impl

  vec3 finalCol = centerCol;
  out_fragColor = vec4(ENCODE_AA_RESULT(finalCol), 1.f);
}

