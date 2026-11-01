import postfx_vs;
import tonemapping;
import constants;
import common;

struct FsOutput
{
  var fragColor: float4   : SV_Target0;
  var linearColor: float4 : SV_Target1;
}

uniform let ldrImage: Sampler2D<float4>;
uniform let prevFrame: Sampler2D<float4>;
uniform let motionVectors: Sampler2D<float3>;
uniform let gbufDepth: Sampler2D<float>;

uniform let constants: ConstantBuffer<Constants>;
uniform let viewParams: ConstantBuffer<ViewParams>;

static const float3x3 MITCHELL_3X3 = float3x3(
  0.00308642,
  0.04938272,
  0.00308642,
  0.04938272,
  0.79012346,
  0.04938272,
  0.00308642,
  0.04938272,
  0.00308642);

func closest_depth_init(params: ViewParams) -> float
{
  if (params.needReverseZ)
    return -9999.f;
  else
    return 9999.f;
}

func closest_depth_compare(cd: float, nd: float, params: ViewParams) -> bool
{
  return params.needReverseZ ? (nd > cd) : (nd < cd);
}

struct NeighbourhoodData
{
  var sourceSample: float3;
  var clipBoxMin: float3;
  var clipBoxMax: float3;
  var clampBoxMin: float3;
  var clampBoxMax: float3;
  var dilatedMotionVector: float2;
  var convergenceFactor: float;
  var closestDepth: float;
}

func sample_neighbourhood(uv: float2, params: ViewParams) -> NeighbourhoodData
{
  var nd: NeighbourhoodData;
  var minCol = float3(9999.f);
  var maxCol = float3(-9999.f);
  var m1 = float3(0.f);
  var m2 = float3(0.f);
  nd.closestDepth = closest_depth_init(params);
  nd.sourceSample = float3(0.f);
  var closestDepthUv: float2 = uv;
  for (var x: int = -1; x <= 1; ++x)
    for (var y: int = -1; y <= 1; ++y)
    {
      var neiUv: float2 = uv + float2(x, y) * constants.mainTargetInverseResolution;
      neiUv = clamp(neiUv, 0.f, 1.f);
      let nc = ldrImage.SampleLevel(neiUv, 0).xyz;
      minCol = min(minCol, nc);
      maxCol = max(maxCol, nc);
      m1 += nc;
      m2 += nc * nc;
      let w = MITCHELL_3X3[x + 1][y + 1];
      nd.sourceSample += nc * w;
      let d = gbufDepth.SampleLevel(neiUv, 0).x;
      if (closest_depth_compare(nd.closestDepth, d, params))
      {
        nd.closestDepth = d;
        closestDepthUv = neiUv;
      }
    }
  let mu = m1 / 9.f;
  let sigma = sqrt(abs((m2 / 9.f) - (mu * mu)));
  let gamma = 1.f;
  nd.dilatedMotionVector = motionVectors.SampleLevel(closestDepthUv, 0).xy;
  nd.convergenceFactor = motionVectors.SampleLevel(uv, 0).z;
  nd.clampBoxMin = minCol;
  nd.clampBoxMax = maxCol;
  nd.clipBoxMin = mu - gamma * sigma;
  nd.clipBoxMax = mu + gamma * sigma;
  return nd;
}

func clip_to_aabb_center(pnt: float3, bbmin: float3, bbmax: float3) -> float3
{
  let center = 0.5f * (bbmax + bbmin);
  let extent = 0.5f * (bbmax - bbmin);
  let v = pnt - center;
  let vn = v / extent;
  let avn = abs(vn);
  let maxNormOff = max(avn.x, max(avn.y, avn.z));
  if (maxNormOff > 1.f)
    return center + v / maxNormOff;
  else
    return pnt; // inside
}

[shader("fragment")]
func taa_fragment_main(texCoord: float2 : TEXCOORD) -> FsOutput
{
  let neiData = sample_neighbourhood(texCoord, viewParams);
  let curCol = neiData.sourceSample;
  var finalCol: float3 = curCol;

  if (constants.frameNo > 0)
  {
    let uv = texCoord - neiData.dilatedMotionVector;
    if (uv.x >= 0.f && uv.x <= 1.f && uv.y >= 0.f && uv.y <= 1.f)
    {
      var prevCol: float3 = sample_tex_catmull_rom_5tap(prevFrame, uv, prevFrame.getSize(), 0.4f).xyz;

      prevCol = clamp(prevCol, neiData.clampBoxMin, neiData.clampBoxMax);
      prevCol = clip_to_aabb_center(prevCol, neiData.clipBoxMin, neiData.clipBoxMax);

      var sourceW: float = neiData.convergenceFactor;
      var histW: float = 1.f - sourceW;
      let curCompressed = curCol / (max(curCol.x, max(curCol.y, curCol.z)) + 1.f);
      let prevCompressed = prevCol / (max(prevCol.x, max(prevCol.y, prevCol.z)) + 1.f);
      let curLuminance = ldr_luminance(curCompressed);
      let prevLuminance = ldr_luminance(prevCompressed);
      sourceW /= curLuminance + 1.f;
      histW /= prevLuminance + 1.f;

      finalCol = (curCol * sourceW + prevCol * histW) / max(sourceW + histW, 0.0001f);
    }
  }

  return FsOutput(
    encode_aa_result(finalCol, constants),
    float4(finalCol, 1.f));
}
