#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "constants.h"


layout(location = 0) out vec4 out_fragColor;

layout(binding = 0) uniform sampler2D hdrImage;

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

void main(void)
{
  // @TODO: real tonemapping
  vec3 ldrColor = clamp(textureLod(hdrImage, surf.texCoord, 0.f).xyz, 0.f, 1.f);

  if (constants.useTonemapping != 0)
  {
    // @TEST
    ldrColor.y = 0.f;
  }

  out_fragColor = vec4(ldrColor, 1.f);
}
