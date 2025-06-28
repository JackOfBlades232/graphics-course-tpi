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

BBox get_inst_bbox(const CullableInstance inst)
{
  // @TODO: more robust -- if we have many chunk types, commandId will not be a proper base
  if ((inst.flags & TERRAIN_CHUNK_INSTANCE_FLAG) != 0)
    return bboxes[inst.commandId + inst.instId];
  else
    return bboxes[inst.commandId];
}

mat4 get_inst_matrix(const CullableInstance inst, const vec3 w_pos)
{
  // @TODO: if it is a bottleneck, do faster aabb correct culling for chunks,
  // as they are always axis aligned

  // Bboxes are already world-space for chunks relative to player.
  if ((inst.flags & TERRAIN_CHUNK_INSTANCE_FLAG) != 0)
    return translation(vec3(w_pos.x, 0.f, w_pos.z));
  else
    return instanceMatrices[inst.instId];
}

#endif // INSTANCING_H_INCLUDED
