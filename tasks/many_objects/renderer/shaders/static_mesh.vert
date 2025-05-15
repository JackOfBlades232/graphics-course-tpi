#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "materials.h"
#include "quantization.h"


layout(location = 0) in vec4 vPosNorm;
layout(location = 1) in vec4 vTexCoordAndTang;

layout(push_constant) uniform params_t
{
  mat4 mProjView;
  mat4 mModelAndMatId;
} params;


layout (location = 0 ) out VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec4 wTangent;
  vec2 texCoord;
} vOut;

out gl_PerVertex { vec4 gl_Position; };

mat4 mat4_clear_low_row(mat4 m)
{
  mat4 mm = m;
  mm[0].w = 0.0f;
  mm[1].w = 0.0f;
  mm[2].w = 0.0f;
  mm[3].w = 1.0f;
  return mm;
}

void main(void)
{
  const mat4 modelMatrix = mat4_clear_low_row(params.mModelAndMatId);

  const vec4 wNorm = vec4(dequantize3fnorm(floatBitsToInt(vPosNorm.w)), 0.0f);
  const vec4 wTang = dequantize3f1snorm(floatBitsToInt(vTexCoordAndTang.z));

  vOut.wPos   = (modelMatrix * vec4(vPosNorm.xyz, 1.0f)).xyz;
  vOut.wNorm  = normalize(mat3(transpose(inverse(modelMatrix))) * wNorm.xyz);
  vOut.wTangent = vec4(normalize(mat3(transpose(inverse(modelMatrix))) * wTang.xyz), wTang.w);
  vOut.texCoord = vTexCoordAndTang.xy;

  gl_Position   = params.mProjView * vec4(vOut.wPos, 1.0);
}
