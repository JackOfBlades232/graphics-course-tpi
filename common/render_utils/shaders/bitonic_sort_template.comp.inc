#include "defs.h"

layout(local_size_x = BASE_WORK_GROUP_SIZE) in;

layout(push_constant) uniform params_t
{
  uint shiftPow;
  uint dirSwitchRunPow;
} params;

layout(binding = 0, set = 0) buffer data_t
{
  TYPE data[];
};

// @TODO:
// make suitable for NPOT
// optimize -- this tanks framerate like crazy

void main(void)
{
  const uint globId = uint(gl_GlobalInvocationID.x);
  const uint dirGroupId = globId >> params.dirSwitchRunPow;
  const uint shiftGroupId = globId >> params.shiftPow;
  const uint shift = 1 << params.shiftPow;
  const uint inShiftGroupId = globId - shiftGroupId * shift;

  const uint first = shiftGroupId * shift * 2 + inShiftGroupId;
  const uint second = first + shift;

  if (second < data.length())
  {
    const TYPE dfirst = data[first];
    const TYPE dsecond = data[second];

    const bool shouldSwap =
      (dirGroupId & 1) == 1 ? LESS(dfirst, dsecond) : LESS(dsecond, dfirst);

    if (shouldSwap)
    {
      data[first] = dsecond;
      data[second] = dfirst;
    }
  }
}

