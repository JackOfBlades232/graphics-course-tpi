import common;
import constants;

func expand_bbox(base: BBox, addition: float) -> BBox
{
  let ext = base.max.xyz - base.min.xyz;
  var expanded = base;
  expanded.min.xyz -= addition * ext;
  expanded.max.xyz += addition * ext;
  return expanded;
}

func get_inst_bbox(inst: CullableInstance, bboxes: StructuredBuffer<BBox>) -> BBox
{
  if (flagset(inst.flags, InstanceFlags::WATER_CHUNK))
    return expand_bbox(bboxes[inst.instId], 1.f);
  if (flagset(inst.flags, InstanceFlags::TERRAIN_CHUNK))
    return bboxes[inst.instId];
  else
    return bboxes[inst.commandId];
}

func get_inst_matrix(
  inst: CullableInstance,
  instance_matrices: StructuredBuffer<float4x4>,
  constants: Constants) -> float4x4
{
  // Bboxes are already world-space for chunks relative to toroidal center.
  if (anyflagset(inst.flags, InstanceFlags::TERRAIN_CHUNK | InstanceFlags::WATER_CHUNK))
    return translation4x4(float3(constants.toroidalUpdatePlayerWorldPos.x, 0.f, constants.toroidalUpdatePlayerWorldPos.y));
  else
    return instance_matrices[inst.instId];
}
