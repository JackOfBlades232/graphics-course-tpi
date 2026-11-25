import constants;

// @TODO:
// study the algos
// look into improving module structure via interface and impls
// look into improving code reuse and generics safety
// complete more builtin type extensions
//
// rewrite app code to grab thread group sizes from info

static const int INT8_MIN = -128;
static const int INT8_MAX = 127;

static const int INT16_MIN = -32768;
static const int INT16_MAX = 32767;

static const int INT_MIN = (-2147483647 - 1);
static const int INT_MAX = 2147483647;

static const uint UINT8_MAX = 255u;
static const uint UINT16_MAX = 65535u;
static const uint UINT_MAX = 0xFFFFFFFFu;

static const int64_t INT64_MAX = 9223372036854775807ll;
static const uint64_t UINT64_MAX = 0xFFFFFFFFFFFFFFFFull;

static const float FLT_MAX = 3.402823466e+38f;
static const float FLT_MIN_NORMAL = 1.175494351e-38f;

static const float EPSILON = 0.00001f;
static const float BIG_EPSILON = 0.001f;

static const float PI = 3.1415927f;

func flagset(flags: uint, flag: uint) -> bool
{
  return (flags & flag) == flag;
}

func anyflagset(flags: uint, mask: uint) -> bool
{
  return (flags & mask) != 0;
}

interface ITexture1D
{
  func getSize() -> uint;
}

interface ITexture2D
{
  func getSize() -> uint2;
}

interface ITexture3D
{
  func getSize() -> uint3;
}

extension<T: ITexelElement> Sampler2D<T> : ITexture2D
{
  func getSize() -> uint2
  {
    var width: uint;
    var height: uint;
    GetDimensions(width, height);
    return uint2(width, height);
  }
}

extension<T: ITexelElement> RWTexture3D<T> : ITexture3D
{
  func getSize() -> uint3
  {
    var width: uint;
    var height: uint;
    var depth: uint;
    GetDimensions(width, height, depth);
    return uint3(width, height, depth);
  }
}

interface IBuffer
{
  func getElementCount() -> uint;
}

extension<T> StructuredBuffer<T> : IBuffer
{
  func getElementCount() -> uint
  {
    var numStructs: uint;
    var stride: uint;
    GetDimensions(numStructs, stride);
    return numStructs;
  }
}

extension<T> RWStructuredBuffer<T> : IBuffer
{
  func getElementCount() -> uint
  {
    var numStructs: uint;
    var stride: uint;
    GetDimensions(numStructs, stride);
    return numStructs;
  }
}

func ldr_luminance(col: float3) -> float
{
  return dot(col, float3(0.2127f, 0.7152f, 0.0722f));
}

func ldr_luminance(col: float4) -> float
{
  return ldr_luminance(col.xyz);
}

func luminance_bt601(col: float3) -> float
{
  return dot(col, float3(0.299f, 0.587f, 0.114f));
}

func luminance_bt601(col: float4) -> float
{
  return luminance_bt601(col.xyz);
}

// https://gist.github.com/TheRealMJP/bc503b0b87b643d3505d41eab8b332ae
func sample_tex_catmull_rom_9tap(
  tex_smp: Sampler2D, uv: float2, tex_size: float2) -> float4
{
  let samplePos = uv * tex_size;
  let texPos1 = floor(samplePos - 0.5f) + 0.5f;

  // Compute the fractional offset from our starting texel to our original sample location, which
  // we'll feed into the Catmull-Rom spline function to get our filter weights.
  let f = samplePos - texPos1;

  // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
  // These equations are pre-expanded based on our knowledge of where the texels will be located,
  // which lets us avoid having to evaluate a piece-wise function.
  let w0 = f * (-0.5f + f * (1.f - 0.5f * f));
  let w1 = 1.f + f * f * (-2.5f + 1.5f * f);
  let w2 = f * (0.5f + f * (2.f - 1.5f * f));
  let w3 = f * f * (-0.5f + 0.5f * f);

  // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
  // simultaneously evaluate the middle 2 samples from the 4x4 grid.
  let w12 = w1 + w2;
  let offset12 = w2 / (w1 + w2);

  // Compute the final UV coordinates we'll use for sampling the texture
  var texPos0: float2 = texPos1 - 1;
  var texPos3: float2 = texPos1 + 2;
  var texPos12: float2 = texPos1 + offset12;

  texPos0 /= tex_size;
  texPos3 /= tex_size;
  texPos12 /= tex_size;

  var result = float4(0.f);
  result += tex_smp.SampleLevel(float2(texPos0.x, texPos0.y), 0.f) * w0.x * w0.y;
  result += tex_smp.SampleLevel(float2(texPos12.x, texPos0.y), 0.f) * w12.x * w0.y;
  result += tex_smp.SampleLevel(float2(texPos3.x, texPos0.y), 0.f) * w3.x * w0.y;

  result += tex_smp.SampleLevel(float2(texPos0.x, texPos12.y), 0.f) * w0.x * w12.y;
  result += tex_smp.SampleLevel(float2(texPos12.x, texPos12.y), 0.f) * w12.x * w12.y;
  result += tex_smp.SampleLevel(float2(texPos3.x, texPos12.y), 0.f) * w3.x * w12.y;

  result += tex_smp.SampleLevel(float2(texPos0.x, texPos3.y), 0.f) * w0.x * w3.y;
  result += tex_smp.SampleLevel(float2(texPos12.x, texPos3.y), 0.f) * w12.x * w3.y;
  result += tex_smp.SampleLevel(float2(texPos3.x, texPos3.y), 0.f) * w3.x * w3.y;

  return result;
}

// https://advances.realtimerendering.com/s2016/Filmic%20SMAA%20v7.pptx , p 92
func sample_tex_catmull_rom_5tap(
  tex_smp: Sampler2D, uv: float2, tex_size: float2, c: float) -> float4
{
  let rtMetrics = float4(1.f / tex_size.xy, tex_size.xy);

  let position = rtMetrics.zw * uv;
  let centerPosition = floor(position - 0.5f) + 0.5f;
  let f = position - centerPosition;
  let f2 = f * f;
  let f3 = f * f2;

  let w0 = -c * f3 + 2.f * c * f2 - c * f;
  let w1 = (2.f - c) * f3 - (3.f - c) * f2 + 1.f;
  let w2 = -(2.f - c) * f3 + (3.f - 2.f * c) * f2 + c * f;
  let w3 = c * f3 - c * f2;

  let w12 = w1 + w2;
  let tc12 = rtMetrics.xy * (centerPosition + w2 / w12);
  let centerColor = tex_smp.SampleLevel(float2(tc12.x, tc12.y), 0);

  let tc0 = rtMetrics.xy * (centerPosition - 1.f);
  let tc3 = rtMetrics.xy * (centerPosition + 2.f);
  let color =
    tex_smp.SampleLevel(float2(tc12.x, tc0.y), 0) * (w12.x * w0.y) +
    tex_smp.SampleLevel(float2(tc0.x, tc12.y), 0) * (w0.x * w12.y) +
    centerColor * (w12.x * w12.y) +
    tex_smp.SampleLevel(float2(tc3.x, tc12.y), 0) * (w3.x * w12.y) +
    tex_smp.SampleLevel(float2(tc12.x, tc3.y), 0) * (w12.x * w3.y);
  let w = (w12.x * w0.y) + (w0.x * w12.y) + (w12.x * w12.y) + (w3.x * w12.y) + (w12.x * w3.y);
  return color / w;
}

func float_to_ordered_uint(v: float) -> uint
{
  let i = asuint(v);
  return i ^ ((i & 0x80000000) != 0 ? 0xFFFFFFFF : 0x80000000);
}

func ordered_uint_to_float(i: uint) -> float
{
  return asfloat(i ^ ((i & 0x80000000) != 0 ? 0x80000000 : 0xFFFFFFFF));
}

func dequantize3fnorm(q: uint) -> float3
{
  let aEncX = (q & 0x000000FFu);
  let aEncY = ((q & 0x0000FF00u) >> 8);
  let aEncZ = ((q & 0x00FF0000u) >> 16);

  let usX = uint(aEncX & 0x000000FFu);
  let usY = uint(aEncY & 0x000000FFu);
  let usZ = uint(aEncZ & 0x000000FFu);

  let sX = (usX <= 127) ? int(usX) : (int(usX) - 256);
  let sY = (usY <= 127) ? int(usY) : (int(usY) - 256);
  let sZ = (usZ <= 127) ? int(usZ) : (int(usZ) - 256);

  let x = max(float(sX) / 127.0f, -1.0f);
  let y = max(float(sY) / 127.0f, -1.0f);
  let z = max(float(sZ) / 127.0f, -1.0f);

  return float3(x, y, z);
}

func dequantize3f1snorm(q: uint) -> float4
{
  let aEncX = (q & 0x000000FFu);
  let aEncY = ((q & 0x0000FF00u) >> 8);
  let aEncZ = ((q & 0x00FF0000u) >> 16);
  let aEncOri = ((q & 0xFF000000u) >> 24);

  let usX = uint(aEncX & 0x000000FFu);
  let usY = uint(aEncY & 0x000000FFu);
  let usZ = uint(aEncZ & 0x000000FFu);
  let usOri = uint(aEncOri & 0x000000FFu);

  let sX = (usX <= 127) ? int(usX) : (int(usX) - 256);
  let sY = (usY <= 127) ? int(usY) : (int(usY) - 256);
  let sZ = (usZ <= 127) ? int(usZ) : (int(usZ) - 256);
  let sOri = (usOri <= 127) ? int(usOri) : (int(usOri) - 256);

  let x = max(float(sX) / 127.0f, -1.0f);
  let y = max(float(sY) / 127.0f, -1.0f);
  let z = max(float(sZ) / 127.0f, -1.0f);
  let ori = sOri >= 0.0f ? 1.0f : -1.0f;

  return float4(x, y, z, ori);
}

func dequantize4fcol(c: uint) -> float4
{
  let aEncR = (c & 0x000000FFu);
  let aEncG = ((c & 0x0000FF00u) >> 8);
  let aEncB = ((c & 0x00FF0000u) >> 16);
  let aEncA = ((c & 0xFF000000u) >> 24);
  return float4(
    float(aEncR) / 255.0f, float(aEncG) / 255.0f, float(aEncB) / 255.0f, float(aEncA) / 255.0f);
}

func quantize4fcol(c: float4) -> uint
{
  let ur = uint(c.r * 255.f) & 0xFF;
  let ug = uint(c.g * 255.f) & 0xFF;
  let ub = uint(c.b * 255.f) & 0xFF;
  let ua = uint(c.a * 255.f) & 0xFF;
  return (ua << 24) | (ub << 16) | (ug << 8) | ur;
}

func identity4x4() -> float4x4
{
  return float4x4(
    float4(1.f, 0.f, 0.f, 0.f),
    float4(0.f, 1.f, 0.f, 0.f),
    float4(0.f, 0.f, 1.f, 0.f),
    float4(0.f, 0.f, 0.f, 1.f));
}

func translation4x4(offs: float3) -> float4x4
{
  var m = identity4x4();
  m[3] = float4(offs, 1.f);
  return m;
}

func linearize_z(z: float, n: float, f: float) -> float
{
  return (f * n) / (f - z * (f - n));
}

func linearize_rev_z(z: float, n: float, f: float) -> float
{
  return (f * n) / (n - z * (n - f));
}

func patch_depth_bounds_persp(inout float4x4 pm, n: float, f: float) -> float4x4
{
  let invZRng = 1.f / (f - n);
  pm[2][2] = f * invZRng;
  pm[3][2] = -n * f * invZRng;
  return pm;
}

func patch_depth_bounds_ortho(inout float4x4 pm, n: float, f: float) -> float4x4
{
  let invZRng = 1.f / (f - n);
  pm[2][2] = invZRng;
  pm[3][2] = -n * invZRng;
  return pm;
}

func adjust_viewproj_mat(params: ViewParams, data: ViewData) -> float4x4
{
  if (!params.needDepthBounds)
    return params.mProjView;
  let sceneMinZ = ordered_uint_to_float(data.minViewZOrderedUint) - 0.001f;
  let sceneMaxZ = ordered_uint_to_float(data.maxViewZOrderedUint) + 0.001f;
  let correctedNearZ = max(sceneMinZ, params.viewFrustum.nearZ);
  let correctedFarZ = min(sceneMaxZ, params.viewFrustum.farZ);
  let znr = params.needReverseZ ? correctedFarZ : correctedNearZ;
  let zfr = params.needReverseZ ? correctedNearZ : correctedFarZ;
  var pm: float4x4 = params.mProj;
  if (params.type == ViewType.PERSPECTIVE)
    patch_depth_bounds_persp(pm, znr, zfr);
  else // ORTHO
    patch_depth_bounds_ortho(pm, znr, zfr);
  return mul(params.mView, pm);
}

func get_frustum_split(p: ViewParams, i: uint) -> float
{
  let comp = i & 3;
  switch (comp)
  {
  case 0:
    return p.csmFrustumSplits[i & ~3].x;
  case 1:
    return p.csmFrustumSplits[i & ~3].y;
  case 2:
    return p.csmFrustumSplits[i & ~3].z;
  default:
    return p.csmFrustumSplits[i & ~3].w;
  }
}

func depth_and_tc_to_pos(depth: float, tc: float2, ivpm: float4x4) -> float3
{
  let cameraToScreen = float4(2.f * tc - 1.f, depth, 1.f); 
  let posHom = mul(cameraToScreen, ivpm);
  return posHom.xyz / posHom.w;
}

func unpack_tex_smp(id: TexSmpIdPair) -> uint2
{
  return uint2(uint32_t(id) & 0xFFFF, uint32_t(id) >> 16);
}

struct BindlessSamplerCube<T: ITexelElement> : ITexture2D
{
  DescriptorHandle<TextureCube<T>> tex;
  DescriptorHandle<SamplerState> smp;

  __init(TexSmpIdPair id)
  {
    let ids = unpack_tex_smp(id);
    tex = DescriptorHandle<TextureCube<T>>(ids.x);
    smp = DescriptorHandle<SamplerState>(ids.y);
  }

  func Sample(dir: float3) -> T
  {
    return nonuniform(tex).Sample(nonuniform(smp), dir);
  }

  func SampleLevel(dir: float3, level: float) -> T
  {
    return nonuniform(tex).SampleLevel(nonuniform(smp), dir, level);
  }

  func getSize() -> uint2
  {
    var width: uint;
    var height: uint;
    nonuniform(tex).GetDimensions(width, height);
    return uint2(width, height);
  }
}

struct BindlessSampler2DShadow : ITexture2D
{
  DescriptorHandle<Texture2D> tex;
  DescriptorHandle<SamplerComparisonState> smp;

  __init(TexSmpIdPair id)
  {
    let ids = unpack_tex_smp(id);
    tex = DescriptorHandle<Texture2D>(ids.x);
    smp = DescriptorHandle<SamplerComparisonState>(ids.y);
  }

  func SampleCmpLevelZero(uv: float2, ref: float) -> float
  {
    return nonuniform(tex).SampleCmpLevelZero(nonuniform(smp), uv, ref);
  }

  func getSize() -> uint2
  {
    var width: uint;
    var height: uint;
    nonuniform(tex).GetDimensions(width, height);
    return uint2(width, height);
  }
}

struct BindlessSamplerCubeShadow : ITexture2D
{
  DescriptorHandle<TextureCube> tex;
  DescriptorHandle<SamplerComparisonState> smp;

  __init(TexSmpIdPair id)
  {
    let ids = unpack_tex_smp(id);
    tex = DescriptorHandle<TextureCube>(ids.x);
    smp = DescriptorHandle<SamplerComparisonState>(ids.y);
  }

  func SampleCmpLevelZero(dir: float3, ref: float) -> float
  {
    return nonuniform(tex).SampleCmpLevelZero(nonuniform(smp), dir, ref);
  }

  func getSize() -> uint2
  {
    var width: uint;
    var height: uint;
    nonuniform(tex).GetDimensions(width, height);
    return uint2(width, height);
  }
}

func shadow_technique_is_pcf(tech: ShadowTechnique) -> bool
{
  return tech <= ShadowTechnique.PCF9X9;
}

func shadow_technique_is_pcf_kernel(tech: ShadowTechnique) -> bool
{
  return shadow_technique_is_pcf(tech) && tech >= ShadowTechnique.PCF3X3;
}
