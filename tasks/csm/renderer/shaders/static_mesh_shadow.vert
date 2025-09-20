#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "draw.h"
#include "materials.h"
#include "quantization.h"
#include "constants.h"
#include "geometry.h"
#include "lights.h"


layout(location = 0) in vec4 vPosNorm;
layout(location = 1) in vec4 vTexCoordAndTang;

layout(binding = 0, set = 0) readonly buffer instance_matrices_t
{
  mat4 instanceMatrices[];
};
layout(binding = 1, set = 0) readonly buffer instances_t
{
  DrawableInstance markedInstances[];
};

layout(binding = 9, set = 0) uniform view_params_t
{
  ViewParams viewParams;
};
layout(binding = 10, set = 0) readonly buffer view_data_t
{
  ViewData viewData;
};

layout(location = 0) out VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec4 wTangent;
  vec2 texCoord;
  flat uint matId;
} vOut;

out gl_PerVertex { vec4 gl_Position; };

void main(void)
{
  // @TODO: should not be duplicated with static_mesh.vert
  const DrawableInstance inst = markedInstances[gl_InstanceIndex];
  const mat4 modelMatrix = instanceMatrices[inst.instId];
  const uint matId = inst.materialId;

  const vec4 wNorm = vec4(dequantize3fnorm(floatBitsToInt(vPosNorm.w)), 0.0f);
  const vec4 wTang = dequantize3f1snorm(floatBitsToInt(vTexCoordAndTang.z));

  vOut.wPos     = (modelMatrix * vec4(vPosNorm.xyz, 1.0f)).xyz;
  vOut.wNorm    = normalize(mat3(transpose(inverse(modelMatrix))) * wNorm.xyz);
  vOut.wTangent = vec4(normalize(mat3(transpose(inverse(modelMatrix))) * wTang.xyz), wTang.w);                 
  vOut.texCoord = vTexCoordAndTang.xy;
  vOut.matId    = matId;

  gl_Position   = calc_adjusted_viewproj_mat(viewParams, viewData) * vec4(vOut.wPos, 1.0);
}

