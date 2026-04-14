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

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};
layout(binding = 9, set = 0) uniform view_params_t
{
  ViewParams viewParams;
};
layout(binding = 10, set = 0) readonly buffer view_data_t
{
  ViewData viewData;
};

out gl_PerVertex { vec4 gl_Position; };

void main(void)
{
  const DrawableInstance inst = markedInstances[gl_InstanceIndex];
  const mat4 modelMatrix = instanceMatrices[inst.instId];
  vec3 wPos = (modelMatrix * vec4(vPosNorm.xyz, 1.0f)).xyz;
  gl_Position = calc_adjusted_viewproj_mat(viewParams, viewData) * vec4(wPos, 1.0);
}

