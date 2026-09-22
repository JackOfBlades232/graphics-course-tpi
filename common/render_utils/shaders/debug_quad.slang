import postfx_vs;

struct FsOutput
{
  var color: float4 : SV_Target;
};

uniform var foo: float2;
uniform var fooa: float2[5];
uniform var fooz: float2[0];
// uniform var fooua: float2[];
var bar: Texture2D<float4>;
var baz: ConstantBuffer<float4>;

struct TipiData
{
  var foo: float2;
  var fooa: float2[5];
  var fooz: float2[0];
  // var fooua: float2[];
};

struct Tipi
{
  var data: TipiData;
  var bar: Texture2D<float4>;
  var baz: ConstantBuffer<float4>;
  var bara: Texture2D<float4>[5];
  var barz: Texture2D<float4>[0];
  var barua: Texture2D<float4>[];
};

var tipi: ConstantBuffer<TipiData>;
var ptipi: ParameterBlock<Tipi>;

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
  var color = float4(
    (mask & 1) == 1 ? base.x : 0.f,
    (mask & 2) == 2 ? base.y : 0.f,
    (mask & 4) == 4 ? base.z : 0.f,
    (mask & 8) == 8 ? base.w : 1.f);

  color *= foo.x;
  color += bar.Load(0, 0);
  color += baz;
  color *= tipi.foo.x;
  color *= ptipi.data.foo.x;
  color += ptipi.bar.Load(0, 0);
  color += ptipi.baz;

  return FsOutput(color);
}
