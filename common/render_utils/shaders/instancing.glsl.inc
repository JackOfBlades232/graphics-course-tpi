#ifndef INSTANCING_H_INCLUDED
#define INSTANCING_H_INCLUDED

// @TODO: 4x3
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

BBox get_inst_bbox(const CullableInstance inst)
{
  if ((inst.flags & TERRAIN_CHUNK_INSTANCE_FLAG) != 0)
    return bboxes[inst.instId];
  else
    return bboxes[inst.commandId];
}

mat4 get_inst_matrix(const CullableInstance inst)
{
  // @TODO: if it is a bottleneck, do faster aabb correct culling for chunks,
  // as they are always axis aligned

  // Bboxes are already world-space for chunks relative to toroidal center.
  if ((inst.flags & TERRAIN_CHUNK_INSTANCE_FLAG) != 0)
    return translation(vec3(constants.toroidalUpdatePlayerWorldPos.x, 0.f, constants.toroidalUpdatePlayerWorldPos.y));
  else
    return instanceMatrices[inst.instId];
}

#endif // INSTANCING_H_INCLUDED
