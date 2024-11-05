#version 430
#extension GL_GOOGLE_include_directive : require

#include "UniformParams.h"

layout(location = 0) in vec2 texCoord;
layout(location = 0) out vec4 fragColor;

layout(binding = 7) uniform Params
{
  UniformParams params;
};

void mainImage(out vec4 frag_color, in vec2 frag_coord)
{
  vec2 c1 = vec2(0.25, 0.25);
  vec2 c2 = vec2(0.66, 0.5);
  vec2 c3 = vec2(0.5, 0.8);

  float rline = 0.005;
  float rstep = 0.025;
  float rmod = rline+rstep;

  float speed1 = 0.05;
  float speed2 = 0.08;
  float speed3 = 0.02;

  float off1 = mod(speed1 * params.iTime, rmod);
  float off2 = mod(speed2 * params.iTime, rmod);
  float off3 = mod(speed3 * params.iTime, rmod);

  float r1 = mod(length(frag_coord - c1) - off1, rmod);
  float r2 = mod(length(frag_coord - c2) - off2, rmod);
  float r3 = mod(length(frag_coord - c3) - off3, rmod);

  if (r1 >= rstep || r2 >= rstep || r3 >= rstep)
    frag_color = vec4(vec3(1.5), 1.0);
  else
    frag_color = vec4(vec3(0.75), 1.0);
}

void main()
{
  mainImage(fragColor, texCoord);
}
