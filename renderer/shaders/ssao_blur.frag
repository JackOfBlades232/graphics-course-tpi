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

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

void main()
{
  // @TODO
}

