import postfx_vs;
import tonemapping;
import constants;

struct FsOutput
{
  var color: float4 : SV_Target;
};

uniform let constants: ConstantBuffer<Constants>;

[shader("fragment")]
func reinhard_tonemap_fragment_main(
  texCoord: float2 : TEXCOORD,
  hdrImage: Sampler2D<float4>) -> FsOutput
{
  let hdrColor = hdrImage.SampleLevel(texCoord, 0);

  var ldrColor: float3;

  if (constants.useTonemapping)
    ldrColor = hdrColor.xyz / (1.f + hdrColor.xyz);
  else
    ldrColor = clamp(hdrColor.xyz, 0.f, 1.f);

  return FsOutput(encode_tonemapping_result(ldrColor, constants));
}
