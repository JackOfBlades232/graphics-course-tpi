#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "octahedral.h"
#include "quantization.h"


layout(location = 0) in vec4 vPosNormTexCoordTang;

layout(push_constant) uniform params_t
{
  mat4 mProjView;
  mat4 mModel;
} params;


layout (location = 0 ) out VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec3 wTangent;
  vec2 texCoord;
} vOut;

out gl_PerVertex { vec4 gl_Position; };

void main(void)
{
  // Little endian
  const uint posTcHi = floatBitsToInt(vPosNormTexCoordTang.x);
  const uint posTcLo = floatBitsToInt(vPosNormTexCoordTang.y);

  vec3 lPos;
  vec2 lTc;

  dequantize3f6b2f2b(posTcLo, posTcHi, lPos, lTc);

  const vec4 lNorm = vec4(oct_decode(floatBitsToInt(vPosNormTexCoordTang.z)), 0.0f);
  const vec4 lTang = vec4(oct_decode(floatBitsToInt(vPosNormTexCoordTang.w)), 0.0f);

  vOut.wPos   = (params.mModel * vec4(lPos, 1.0f)).xyz;
  vOut.wNorm  = normalize(mat3(transpose(inverse(params.mModel))) * lNorm.xyz);
  vOut.wTangent = normalize(mat3(transpose(inverse(params.mModel))) * lTang.xyz);
  vOut.texCoord = lTc;

  gl_Position   = params.mProjView * vec4(vOut.wPos, 1.0);
}
