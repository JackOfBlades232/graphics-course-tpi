#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "draw.h"
#include "geometry.h"
#include "constants.h"


layout(binding = 0, set = 0) readonly buffer instance_matrices_t
{
  mat4 instanceMatrices[];
};
layout(binding = 1, set = 0) readonly buffer all_instances_t
{
  CullableInstance allInstances[];
};
layout(binding = 2, set = 0) readonly buffer bboxes_t
{
  BBox bboxes[];
};

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

const uvec2 edges[] = {
  uvec2(0, 1), uvec2(1, 2), uvec2(2, 3), uvec2(0, 3),
  uvec2(4, 5), uvec2(5, 6), uvec2(6, 7), uvec2(4, 7),
  uvec2(0, 4), uvec2(1, 5), uvec2(2, 6), uvec2(3, 7)};

void main()
{
  const CullableInstance inst = allInstances[gl_InstanceIndex];
  const BBox bbox = bboxes[inst.commandId]; 
  const mat4 instMat = instanceMatrices[inst.matrixId];

  const vec4 vs[8] = {
    bbox.min,
    vec4(bbox.max.x, bbox.min.y, bbox.min.z, 1.f),
    vec4(bbox.max.x, bbox.max.y, bbox.min.z, 1.f),
    vec4(bbox.min.x, bbox.max.y, bbox.min.z, 1.f),
    vec4(bbox.min.x, bbox.min.y, bbox.max.z, 1.f),
    vec4(bbox.max.x, bbox.min.y, bbox.max.z, 1.f),
    bbox.max,
    vec4(bbox.min.x, bbox.max.y, bbox.max.z, 1.f)};

  const uvec2 edge = edges[gl_VertexIndex / 2];
  const uint vid = (gl_VertexIndex % 2 == 1) ? edge.y : edge.x;

  gl_Position = constants.mProjView * instMat * vs[vid];
}
