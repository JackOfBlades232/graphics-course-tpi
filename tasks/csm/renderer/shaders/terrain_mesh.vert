#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "draw.h"
#include "materials.h"
#include "constants.h"


// @TODO: figure out why I don't have gl_BaseInstance and use it instead
layout(push_constant) uniform params_t
{
  uint chunksInstBase;
} params;

layout(binding = 0, set = 0) readonly buffer bboxes_t
{
  BBox bboxes[];
};
layout(binding = 1, set = 0) readonly buffer instances_t
{
  DrawableInstance markedInstances[];
};

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

layout(location = 0) out VS_OUT
{
  flat uint chunkId;
} vOut;

void main(void)
{
  const DrawableInstance inst = markedInstances[gl_InstanceIndex];

  const BBox bbox = bboxes[inst.instId];
  const vec3 vs[4] = {
    vec3(bbox.min.x, 0.f, bbox.min.z),
    vec3(bbox.min.x, 0.f, bbox.max.z),
    vec3(bbox.max.x, 0.f, bbox.max.z),
    vec3(bbox.max.x, 0.f, bbox.min.z)};

  const vec3 wPos = vs[gl_VertexIndex] +
    vec3(constants.toroidalUpdatePlayerWorldPos.x, 0.f, constants.toroidalUpdatePlayerWorldPos.y);

  vOut.chunkId = inst.instId - params.chunksInstBase; 

  gl_Position = vec4(wPos, 1.f);
}
