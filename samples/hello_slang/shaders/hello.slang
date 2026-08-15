StructuredBuffer<float> buffer0;
StructuredBuffer<float> buffer1;
RWStructuredBuffer<float> result;

[shader("compute")]
[numthreads(32, 1, 1)]
void computeMain(uint3 threadId : SV_DispatchThreadID)
{
  uint index = threadId.x;
  if (index < result.getCount() && index < buffer0.getCount() && index < buffer1.getCount())
    result[index] = buffer0[index] + buffer1[index];
}
