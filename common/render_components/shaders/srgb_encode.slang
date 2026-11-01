import postfx_vs;
import tonemapping;

struct FsOutput
{
  var color: float4 : SV_Target;
}

[shader("fragment")]
func srgb_encode_fragment_main(
  texCoord: float2 : TEXCOORD,
  ldrImage: Sampler2D<float4>) -> FsOutput
{
  let ldrColor = ldrImage.SampleLevel(texCoord, 0).xyz;
  return FsOutput(float4(gamma_encode(ldrColor), 1.f));
}
