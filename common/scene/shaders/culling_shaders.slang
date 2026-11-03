import common;
import constants;
import instancing;
import culling;

func reset_command(commands: RWStructuredBuffer<IndirectCommand>, id: uint)
{
  if (id < commands.getElementCount())
    commands[id].instanceCount = 0;
}

[shader("compute")]
[numthreads(64, 1, 1)]
func reset_indirect_buf(
  threadId: uint3 : SV_DispatchThreadID,
  commands: RWStructuredBuffer<IndirectCommand>)
{
  reset_command(commands, threadId.x);
}

[shader("compute")]
[numthreads(64, 1, 1)]
func reset_view_context(
  threadId: uint3 : SV_DispatchThreadID,
  commands: RWStructuredBuffer<IndirectCommand>,
  vdata: RWStructuredBuffer<ViewData>)
{
  if (threadId.x == 0)
  {
    vdata[0].prevMinViewZOrderedUint = vdata[0].minViewZOrderedUint;
    vdata[0].prevMaxViewZOrderedUint = vdata[0].maxViewZOrderedUint;
    vdata[0].minViewZOrderedUint = UINT_MAX;
    vdata[0].maxViewZOrderedUint = 0;
  }
  reset_command(commands, threadId.x);
}

[shader("compute")]
[numthreads(64, 1, 1)]
func perform_culling(
  threadId: uint3 : SV_DispatchThreadID,
  instanceMatrices: StructuredBuffer<float4x4>,
  allInstances: StructuredBuffer<CullableInstance>,
  bboxes: StructuredBuffer<BBox>,
  instances: RWStructuredBuffer<DrawableInstance>,
  commands: RWStructuredBuffer<IndirectCommand>,
  constants: ConstantBuffer<Constants>,
  viewParams: ConstantBuffer<ViewParams>)
{
  if (threadId.x < allInstances.getElementCount())
  {
    let inst = allInstances[threadId.x];

    let instMat = get_inst_matrix(inst, instanceMatrices, constants);
    let bbox = get_inst_bbox(inst, bboxes);
    var payload: InstanceViewRelatedData;

    if (instance_is_in_view(bbox, instMat, constants.cullingMode, viewParams, payload))
    {
      var localId: uint;
      InterlockedAdd(commands[inst.commandId].instanceCount, 1, localId);
      let destId = commands[inst.commandId].firstInstance + localId;

      instances[destId].instId = inst.instId;
      instances[destId].materialId = inst.materialId;
      instances[destId].commandId = inst.commandId;
      instances[destId].flags = inst.flags;
      instances[destId].minViewZ = payload.minZ;
      instances[destId].maxViewZ = payload.maxZ;
    }
  }
}

struct MinMax
{
  var minv: float;
  var maxv: float;
}

groupshared var sharedCache: MinMax[64];

[shader("compute")]
[numthreads(64, 1, 1)]
func calculate_depth_bounds(
  threadId: uint3 : SV_DispatchThreadID,
  localThreadId: uint3 : SV_GroupThreadID,
  instances: StructuredBuffer<DrawableInstance>,
  commands: RWStructuredBuffer<IndirectCommand>,
  vdata: RWStructuredBuffer<ViewData>)
{
  let dsize = instances.getElementCount();

  let globId = threadId.x;
  let locId = localThreadId.x;
  let localFirstElem = globId * CALC_DEPTH_BOUNDS_ELEMS_PER_THREAD;
  let localElemCount = min(CALC_DEPTH_BOUNDS_ELEMS_PER_THREAD, dsize - localFirstElem);

  MinMax mm = MinMax(FLT_MAX, -FLT_MAX);
  for (var e: uint = localFirstElem; e < localFirstElem + localElemCount; ++e)
  {
    IndirectCommand cmd = commands[instances[e].commandId];
    if (e < cmd.firstInstance + cmd.instanceCount)
    {
      mm.minv = min(mm.minv, instances[e].minViewZ);
      mm.maxv = max(mm.maxv, instances[e].maxViewZ);
    }
  }

  sharedCache[locId] = mm;

  GroupMemoryBarrierWithGroupSync();

  for (var skip: uint = 1; skip < 64; skip <<= 1)
  {
    if (locId % (skip << 1) == 0)
    {
      let nextId = locId + skip;
      if (nextId < 64)
      {
        sharedCache[locId].minv = min(sharedCache[locId].minv, sharedCache[nextId].minv);
        sharedCache[locId].maxv = max(sharedCache[locId].maxv, sharedCache[nextId].maxv);
      }
    }

    GroupMemoryBarrierWithGroupSync();
  }

  if (locId == 0 && sharedCache[0].minv <= sharedCache[0].maxv)
  {
    let minvOrd = float_to_ordered_uint(sharedCache[0].minv);
    let maxvOrd = float_to_ordered_uint(sharedCache[0].maxv);
    InterlockedMin(vdata[0].minViewZOrderedUint, minvOrd);
    InterlockedMax(vdata[0].maxViewZOrderedUint, maxvOrd);
  }
}
