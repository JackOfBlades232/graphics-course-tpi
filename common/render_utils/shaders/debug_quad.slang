import postfx_vs;

struct FsOutput
{
  var color: float4 : SV_Target;
};

uniform var fooa: float2[5];
uniform var fooz: float2[0];
// uniform var fooua: float2[]; <== @NOTE: this is a compiler bug, and we need to catch it (by hand or spirv-val)

struct A
{
  var a: float;
  var b: float;
};
uniform var as: A[3];
// uniform var asua: A[];

uniform var texarr: Texture2D[2];
uniform var texarrz: Texture2D[0];

uniform var foo: float2;
// var bar: Texture2D<float4>;
// var baz: ConstantBuffer<float4>;

struct TipiData
{
  var foo: float2;
  var fooa: float2[5];
  var fooz: float2[0];
  // var fooua: float2[]; <== @NOTE and this is an overrestriction of slang
};
struct Tipi
{
  var data: TipiData;
  var bar: Texture2D<float4>;
  var baz: ConstantBuffer<float4>;
  var bara: Texture2D<float4>[5];
  var barz: Texture2D<float4>[0];
  //var barua: Texture2D<float4>[];
  Texture2D[] barua;
};

struct MixedResources
{
  var foo: float4;
  var bar: StructuredBuffer<float4>;
  var baz: RWTexture2D<float4>;
  var tipi: ConstantBuffer<TipiData>;
  // var ptipi: ParameterBlock<Tipi>; <== @NOTE at least this is illegal
};
uniform var mixeda: MixedResources[5];

// This is done in the following way -- when I slap an array in (however nested),
// any resource name accessed as a.b.c[N].d[M].l[K] becomes a solo binding for a.b.c.d.l and an [N*M*K] array
// and numeric data behaves like we expect. So, builder API should give us the hierarchy view, but fill at correct strides

var tipi: ConstantBuffer<TipiData>;

// uniform var asua: A[]; <== @NOTE and this (as last member) is a bug in spirv codegen. All in all, have to ZAPRETIT!

// Failed attempt
// struct Pool<T>
// {
//   struct Entry
//   {
//     var data: T = {};
//     var next: int = -1;
//   };
//
//   var head: int;
//   var entries: Entry[];
// };
//
// var matrixPool: RWStructuredBuffer<Pool<float4x4>>;

// @NOTE: so, verdicts about unsized arrays of numerics -- BAN. They do not work (unless I go into deep compat syntax, idk)

var tipis: ConstantBuffer<TipiData>[5];

var ptipi: ParameterBlock<Tipi>;

var texarrua: Texture2D[];

[shader("fragment")]
func debugQuadFragmentMain(
  texCoord: float2 : TEXCOORD
  // ,
  // colorTex: Sampler2D<float4>,
  // uniform colorRange: float2,
  // uniform mask: uint
  ) -> FsOutput
{
  // let base =
  //   (colorTex.SampleLevel(texCoord, 0.f) - colorRange.x) /
  //   (colorRange.y - colorRange.x);
  // var color = float4(
  //   (mask & 1) == 1 ? base.x : 0.f,
  //   (mask & 2) == 2 ? base.y : 0.f,
  //   (mask & 4) == 4 ? base.z : 0.f,
  //   (mask & 8) == 8 ? base.w : 1.f);
  var color = float4(2.f);

  color *= foo.x;
  color *= fooa[2].x;
  // color *= fooua[128].x;
  color *= as[1].b;
  // color *= asua[128].a;
  color += texarr[0].Load(0, 0);
  color += texarr[1].Load(0, 0);
  color += texarrua[128].Load(0, 0);
  color += texarrua[0].Load(0, 0);
  color *= tipi.foo.x;
  color *= tipi.fooa[2].x;
  // color += bar.Load(0, 0);
  // color += baz;
  // color *= tipi.foo.x;
  color *= ptipi.data.foo.x;
  color += ptipi.bar.Load(0, 0);
  color += ptipi.baz;
  color += ptipi.barua[2244].Load(0, 0);

  // if (matrixPool.head >= 0)
  // {
  //   color += matrixPool.entries[matrixPool.head].data[0];
  // }

  color += mixeda[3].foo.x;
  color += mixeda[3].bar[0];
  color += mixeda[3].baz.Load(0, 0);
  color += mixeda[3].tipi.fooa[3].x;

  color *= tipis[4].foo.x;
  color *= tipis[4].fooa[2].x;

  color += texarrua[2].Load(0, 0);

  return FsOutput(color);
}
