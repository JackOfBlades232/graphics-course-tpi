import postfx_vs;

struct FsOutput
{
  var color: float4 : SV_Target;
};

[shader("fragment")]
func debugQuadFragmentMain(
  texCoord: float2 : TEXCOORD,
  colorTex: Sampler2D<float4>,
  uniform colorRange: float2,
  uniform mask: uint) -> FsOutput
{
  let base =
    (colorTex.SampleLevel(texCoord, 0.f) - colorRange.x) /
    (colorRange.y - colorRange.x);
  let color = float4(
    (mask & 1) == 1 ? base.x : 0.f,
    (mask & 2) == 2 ? base.y : 0.f,
    (mask & 4) == 4 ? base.z : 0.f,
    (mask & 8) == 8 ? base.w : 1.f);
  return FsOutput(color);
}
