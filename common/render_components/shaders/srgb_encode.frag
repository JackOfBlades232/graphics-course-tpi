#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

layout(location = 0) out vec4 out_fragColor;

layout(binding = 0) uniform sampler2D ldrImage;

#include "tonemap.frag.inc"

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

void main(void)
{
  const vec3 ldrColor = textureLod(ldrImage, surf.texCoord, 0.f).xyz;
  out_fragColor = vec4(gamma_encode(ldrColor), 1.f);
}

