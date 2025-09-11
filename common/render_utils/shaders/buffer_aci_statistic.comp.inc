#include "defs.h"

layout(local_size_x = BASE_WORK_GROUP_SIZE) in;

layout(binding = 0, set = 0) readonly buffer data_t
{
  TYPE data[];
};

layout(binding = 1, set = 0) buffer result_t
{
  RTYPE result;
};

shared RTYPE sharedCache[BASE_WORK_GROUP_SIZE];

void main(void)
{
  const int dsize = int(data.length());

  const int globId = int(gl_GlobalInvocationID.x);
  const int locId = int(gl_LocalInvocationID.x);
  const int localFirstElem = globId * BUFFER_ACI_ELEMS_PER_THREAD;
  const int localElemCount = min(BUFFER_ACI_ELEMS_PER_THREAD, dsize - localFirstElem);

  RTYPE localAccum = INIT;
  for (int e = localFirstElem; e < localFirstElem + localElemCount; ++e)
    localAccum = ADD_OP(localAccum, data[e]);

  sharedCache[locId] = localAccum;

  barrier();

  for (int skip = 1; skip < BASE_WORK_GROUP_SIZE; skip <<= 1)
  {
    if (locId % skip == 0)
    {
      const int nextId = locId + skip;
      if (nextId < BASE_WORK_GROUP_SIZE)
        sharedCache[locId] = MERGE_OP(sharedCache[locId], sharedCache[nextId]); 
    }

    barrier();
  }

  if (locId == 0 && VALID(sharedCache[0]))
    ATOMIC_OP(result, sharedCache[0]);
}


