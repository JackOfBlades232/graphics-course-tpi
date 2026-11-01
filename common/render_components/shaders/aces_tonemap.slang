import postfx_vs;
import tonemapping;
import constants;

struct FsOutput
{
  var color: float4 : SV_Target;
}

uniform let constants: ConstantBuffer<Constants>;

// https://knarkowicz.wordpress.com/2016/01/06/aces-filmic-tone-mapping-curve/
func aces_film(x: float3) -> float3
{
  let a = 2.51f;
  let b = 0.03f;
  let c = 2.43f;
  let d = 0.59f;
  let e = 0.14f;
  return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.f, 1.f);
}

[shader("fragment")]
func aces_tonemap_fragment_main(
  texCoord: float2 : TEXCOORD,
  hdrImage: Sampler2D<float4>) -> FsOutput
{
  let hdrColor = hdrImage.SampleLevel(texCoord, 0);

  var ldrColor: float3;

  if (constants.useTonemapping)
    ldrColor = aces_film(hdrColor.xyz * constants.acesExposure);
  else
    ldrColor = clamp(hdrColor.xyz, 0.f, 1.f);

  return FsOutput(encode_tonemapping_result(ldrColor, constants));
}
