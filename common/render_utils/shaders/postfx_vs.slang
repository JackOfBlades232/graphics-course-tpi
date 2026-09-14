struct VsOutput
{
  var pos:      float4 : SV_Position; 
  var texCoord: float2 : TEXCOORD;
};

[shader("vertex")]
func postfxVertexMain(uint vid : SV_VertexID) -> VsOutput
{
  var vOut: VsOutput = {};
  float2 xy = vid == 0.f ? float2(-1.f, -1.f) : (vid == 1.f ? float2(3.f, -1.f) : float2(-1.f, 3.f));
  vOut.pos = float4(xy, 0.f, 1.f);
  vOut.texCoord = xy * 0.5f + 0.5f;
  return vOut;
}
