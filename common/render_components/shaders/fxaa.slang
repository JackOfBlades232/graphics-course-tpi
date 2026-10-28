// @TODO:
// Constants & type headers for slang (minimal tree, need refactoring)
//  Research slang's capability of emitting c++ instead
// Factor out common code

import postfx_vs;
import tonemapping;

struct FsOutput
{
  var color: float4 : SV_Target;
};

uniform let constants: ConstantBuffer<Constants>;

static const bool DEBUG_CUTOFF_PIXELS = false;
static const bool DEBUG_SOLID_COLOR = false;
static const bool DEBUG_EDGE_DIRECTION = false;
static const bool DEBUG_SUBPIXEL_W = false;

func fxaa_luma(rgb: float3) -> float
{
  if (constants.fxaaAntialiasInSrgb != 0)
    return rgb.y * (0.587f / 0.299f) + rgb.x;
  else
    return sqrt(dot(rgb, float3(0.299f, 0.587f, 0.114f)));
}

func fxaa_high_pass_filter(l: float, c: float, r: float) -> float
{
  return abs(0.25f * l - 0.5f * c + 0.25f * r);
}

func fxaa311_high_pass_filter(center: float, corners: float ) -> float
{
  return abs(-2.f * center + corners);
}

static const float FXAA_EDGE_THRESHOLD_MIN = 1.f / 16.f;
static const float FXAA_EDGE_THRESHOLD = 1.f / 8.f;
static const uint FXAA_SEARCH_STEPS = 16;
static const float FXAA_SUBPIXEL_TRIM = 0.25f;
static const float FXAA_SUBPIXEL_TRIM_SCALE = 1.f / (1.f - FXAA_SUBPIXEL_TRIM);
static const float FXAA_SUBPIXEL_CAP = 0.75f;

[shader("fragment")]
func fxaa_fragment_main(
  texCoord: float2 : TEXCOORD,
  ldrImage: Sampler2D<float4>) -> FsOutput
{
  let ccol = ldrImage.SampleLevel(texCoord, 0).xyz;
  let cluma = fxaa_luma(ccol);

  let ncol = ldrImage.SampleLevel(texCoord, 0, int2(0, -1)).xyz;
  let scol = ldrImage.SampleLevel(texCoord, 0, int2(0, 1)).xyz;
  let wcol = ldrImage.SampleLevel(texCoord, 0, int2(-1, 0)).xyz;
  let ecol = ldrImage.SampleLevel(texCoord, 0, int2(1, 0)).xyz;

  let nluma = fxaa_luma(ncol);
  let sluma = fxaa_luma(scol);
  let wluma = fxaa_luma(wcol);
  let eluma = fxaa_luma(ecol);

  let lumaMin = min(cluma, min(min(nluma, sluma), min(wluma, eluma)));
  let lumaMax = max(cluma, max(max(nluma, sluma), max(wluma, eluma)));
  let lumaRng = lumaMax - lumaMin;

  if (lumaRng < max(FXAA_EDGE_THRESHOLD_MIN, lumaMax * FXAA_EDGE_THRESHOLD))
  {
    if (DEBUG_CUTOFF_PIXELS)
      return FsOutput(encode_aa_result(float3(0.2f, 0.2f, 0.2f), constants));
    else
      return FsOutput(encode_aa_result(ccol, constants));
  }

  let nwcol = ldrImage.SampleLevel(texCoord, 0, int2(-1, -1)).xyz;
  let swcol = ldrImage.SampleLevel(texCoord, 0, int2(-1, 1)).xyz;
  let necol = ldrImage.SampleLevel(texCoord, 0, int2(1, -1)).xyz;
  let secol = ldrImage.SampleLevel(texCoord, 0, int2(1, 1)).xyz;

  let nwluma = fxaa_luma(nwcol);
  let swluma = fxaa_luma(swcol);
  let neluma = fxaa_luma(necol);
  let seluma = fxaa_luma(secol);

  // edge
  let edgeVert =
    fxaa_high_pass_filter(nwluma, nluma, neluma) +
    2.f * fxaa_high_pass_filter(wluma, cluma, eluma) +
    fxaa_high_pass_filter(swluma, sluma, seluma);
  let edgeHoriz =
    fxaa_high_pass_filter(swluma, wluma, nwluma) +
    2.f * fxaa_high_pass_filter(sluma, cluma, nluma) +
    fxaa_high_pass_filter(seluma, eluma, neluma);

  let isHoriz = edgeHoriz >= edgeVert;

  if (DEBUG_EDGE_DIRECTION)
  {
    if (isHoriz)
      return FsOutput(encode_aa_result(float3(0.9f, 0.f, 0.1f), constants));
    else
      return FsOutput(encode_aa_result(float3(0.f, 0.9f, 0.1f), constants));
  }

  let neiLuma1 = isHoriz ? nluma : wluma;
  let neiLuma2 = isHoriz ? sluma : eluma;

  let neiGrad1 = neiLuma1 - cluma;
  let neiGrad2 = neiLuma2 - cluma;

  let nei1IsHigherContrast = abs(neiGrad1) >= abs(neiGrad2);

  let hierNeiGrad = nei1IsHigherContrast ? neiGrad1 : neiGrad2;
  let hierNeiLuma = nei1IsHigherContrast ? neiLuma1 : neiLuma2;

  let gradCutoff = 0.25f * abs(hierNeiGrad);
  let lumaLocalAv = 0.5f * (cluma + hierNeiLuma);

  let offBase =
    (nei1IsHigherContrast ? -1.f : 1.f) *
    (isHoriz ? float2(0.f, 1.f) : float2(1.f, 0.f));
  let off = 0.5f * constants.mainTargetInverseResolution * offBase;
  let stpBase = (isHoriz ? float2(1.f, 0.f) : float2(0.f, 1.f));
  let stp = constants.mainTargetInverseResolution * stpBase;

  var doneP: bool = false;
  var doneN: bool = false;

  var lumaEndP: float;
  var lumaEndN: float;

  var offP: float2 = off - stp;
  var offN: float2 = off + stp;

  [unroll]
  for (var i: uint = 0; i < FXAA_SEARCH_STEPS; ++i)
  {
    if (!doneP)
      lumaEndP = fxaa_luma(ldrImage.SampleLevel(texCoord + offP, 0).xyz);
    if (!doneN)
      lumaEndN = fxaa_luma(ldrImage.SampleLevel(texCoord + offN, 0).xyz);
    doneP = doneP || (abs(lumaEndP - lumaLocalAv) >= gradCutoff);
    doneN = doneN || (abs(lumaEndN - lumaLocalAv) >= gradCutoff);
    if (doneP && doneN)
      break;
    if (!doneP)
      offP -= stp;
    if (!doneN)
      offN += stp;
  }

  let dP = isHoriz ? -offP.x : -offP.y;
  let dN = isHoriz ? offN.x : offN.y;

  let pIsClosest = dP < dN;
  let closestD = min(dP, dN);
  let edgeW = dP + dN;
  let closestLuma = pIsClosest ? lumaEndP : lumaEndN;

  let sampleOffsetAlongEdge = 0.5f - closestD / edgeW;

  let lumaCIsSmaller = cluma < lumaLocalAv;
  let edgeEndIsSameAsNei = lumaCIsSmaller ? (closestLuma > lumaLocalAv) : (closestLuma < lumaLocalAv);

  let sampleTc =
    texCoord + (edgeEndIsSameAsNei ? (offBase * constants.mainTargetInverseResolution * sampleOffsetAlongEdge) : float2(0.f));

  let edgeAntialiasedCol = ldrImage.SampleLevel(sampleTc, 0).xyz;

  // subpixel
  let lowpassLuma = (nluma + sluma + wluma + eluma) * 0.25f;
  let lowpassRange = abs(lowpassLuma - cluma);
  let subpixelBlend =
    min(
      max(0.f, (lowpassRange / lumaRng) - FXAA_SUBPIXEL_TRIM) * FXAA_SUBPIXEL_TRIM_SCALE,
      FXAA_SUBPIXEL_CAP);

  if (DEBUG_SUBPIXEL_W)
  {
    return FsOutput(encode_aa_result(float3(subpixelBlend + 0.2f), constants));
  }

  if (DEBUG_SOLID_COLOR)
  {
    return FsOutput(encode_aa_result(float3(0.9f, 0.9f, 0.9f), constants));
  }

  let rgbLowpassFull =
    (ccol + ncol + scol + wcol + ecol + nwcol + swcol + necol + secol) * (1.f / 9.f);

  let finalCol = lerp(edgeAntialiasedCol, rgbLowpassFull, subpixelBlend);

  return FsOutput(encode_aa_result(finalCol, constants));
}

static const float FXAA311_EDGE_THRESHOLD_MIN = 0.0312f;
static const float FXAA311_EDGE_THRESHOLD_MAX = 0.125f;
static const uint FXAA311_SEARCH_STEPS = 11;
static const float FXAA311_SEARCH_QUAL[FXAA311_SEARCH_STEPS] =
{
  1.f, 1.f, 1.f, 1.f, 1.5f, 2.f, 2.f, 2.f, 2.f, 4.f, 8.f
};
static const float FXAA311_SUBPIXEL_QUAL = 0.75f;

[shader("fragment")]
func fxaa311_fragment_main(
  texCoord: float2 : TEXCOORD,
  ldrImage: Sampler2D<float4>) -> FsOutput
{
  let ccol = ldrImage.SampleLevel(texCoord, 0.f).xyz;
  let cluma = fxaa_luma(ccol);

  let ncol = ldrImage.SampleLevel(texCoord, 0, int2(0, -1)).xyz;
  let scol = ldrImage.SampleLevel(texCoord, 0, int2(0, 1)).xyz;
  let wcol = ldrImage.SampleLevel(texCoord, 0, int2(-1, 0)).xyz;
  let ecol = ldrImage.SampleLevel(texCoord, 0, int2(1, 0)).xyz;

  let nluma = fxaa_luma(ncol);
  let sluma = fxaa_luma(scol);
  let wluma = fxaa_luma(wcol);
  let eluma = fxaa_luma(ecol);

  let lumaMin = min(cluma, min(min(nluma, sluma), min(wluma, eluma)));
  let lumaMax = max(cluma, max(max(nluma, sluma), max(wluma, eluma)));
  let lumaRng = lumaMax - lumaMin;

  if (lumaRng < max(FXAA311_EDGE_THRESHOLD_MIN, lumaMax * FXAA311_EDGE_THRESHOLD_MAX))
  {
    if (DEBUG_CUTOFF_PIXELS)
      return FsOutput(encode_aa_result(float3(0.2f, 0.2f, 0.2f), constants));
    else
      return FsOutput(encode_aa_result(ccol, constants));
  }

  let nwcol = ldrImage.SampleLevel(texCoord, 0, int2(-1, -1)).xyz;
  let swcol = ldrImage.SampleLevel(texCoord, 0, int2(-1, 1)).xyz;
  let necol = ldrImage.SampleLevel(texCoord, 0, int2(1, -1)).xyz;
  let secol = ldrImage.SampleLevel(texCoord, 0, int2(1, 1)).xyz;

  let nwluma = fxaa_luma(nwcol);
  let swluma = fxaa_luma(swcol);
  let neluma = fxaa_luma(necol);
  let seluma = fxaa_luma(secol);

  let udluma = nluma + sluma;
  let lrluma = wluma + eluma;
  let lcluma = nwluma + swluma;
  let dcluma = swluma + seluma;
  let rcluma = neluma + seluma;
  let ucluma = nwluma + neluma;

  // edge
  let edgeVert =
    fxaa311_high_pass_filter(nluma, ucluma) +
    2.f * fxaa311_high_pass_filter(cluma, lrluma) +
    fxaa311_high_pass_filter(sluma, dcluma);
  let edgeHoriz =
    fxaa311_high_pass_filter(wluma, lcluma) +
    2.f * fxaa311_high_pass_filter(cluma, udluma) +
    fxaa311_high_pass_filter(eluma, rcluma);

  let isHoriz = edgeHoriz >= edgeVert;

  if (DEBUG_EDGE_DIRECTION)
  {
    if (isHoriz)
      return FsOutput(encode_aa_result(float3(0.9f, 0.f, 0.1f), constants));
    else
      return FsOutput(encode_aa_result(float3(0.f, 0.9f, 0.1f), constants));
  }

  let neiLuma1 = isHoriz ? nluma : wluma;
  let neiLuma2 = isHoriz ? sluma : eluma;

  let neiGrad1 = neiLuma1 - cluma;
  let neiGrad2 = neiLuma2 - cluma;

  let nei1IsHigherContrast = abs(neiGrad1) >= abs(neiGrad2);

  let hierNeiGrad = nei1IsHigherContrast ? neiGrad1 : neiGrad2;
  let hierNeiLuma = nei1IsHigherContrast ? neiLuma1 : neiLuma2;

  let gradCutoff = 0.25f * abs(hierNeiGrad);
  let lumaLocalAv = 0.5f * (cluma + hierNeiLuma);

  let offBase =
    (nei1IsHigherContrast ? -1.f : 1.f) *
    (isHoriz ? float2(0.f, 1.f) : float2(1.f, 0.f));
  let off = 0.5f * constants.mainTargetInverseResolution * offBase;
  let stpBase = (isHoriz ? float2(1.f, 0.f) : float2(0.f, 1.f));
  let stp = constants.mainTargetInverseResolution * stpBase;

  var doneP: bool = false;
  var doneN: bool = false;

  var lumaEndP: float;
  var lumaEndN: float;

  var offP: float2 = off - stp;
  var offN: float2 = off + stp;

  [unroll]
  for (var i: uint = 0; i < FXAA311_SEARCH_STEPS; ++i)
  {
    if (!doneP)
      lumaEndP = fxaa_luma(ldrImage.SampleLevel(texCoord + offP, 0).xyz);
    if (!doneN)
      lumaEndN = fxaa_luma(ldrImage.SampleLevel(texCoord + offN, 0).xyz);
    doneP = doneP || (abs(lumaEndP - lumaLocalAv) >= gradCutoff);
    doneN = doneN || (abs(lumaEndN - lumaLocalAv) >= gradCutoff);
    if (doneP && doneN)
      break;
    if (!doneP)
      offP -= stp * FXAA311_SEARCH_QUAL[i];
    if (!doneN)
      offN += stp * FXAA311_SEARCH_QUAL[i];
  }

  let dP = isHoriz ? -offP.x : -offP.y;
  let dN = isHoriz ? offN.x : offN.y;

  let pIsClosest = dP < dN;
  let closestD = min(dP, dN);
  let edgeW = dP + dN;
  let closestLuma = pIsClosest ? lumaEndP : lumaEndN;

  let sampleOffsetAlongEdge = 0.5f - closestD / edgeW;

  let lumaCIsSmaller = cluma < lumaLocalAv;
  let edgeEndIsSameAsNei = lumaCIsSmaller ? (closestLuma > lumaLocalAv) : (closestLuma < lumaLocalAv);

  let finalOff = edgeEndIsSameAsNei ? sampleOffsetAlongEdge : 0.f;

  // subpixel
  let lowpassLuma = (1.f / 12.f) * (2.f * (udluma + lrluma) + lcluma + rcluma);
  let subpixelOffsetBase = clamp(abs(lowpassLuma - cluma) / lumaRng, 0.f, 1.f);
  let subpixelOffsetAdj = smoothstep(0.f, 1.f, subpixelOffsetBase);
  let subpixelOffset = subpixelOffsetAdj * subpixelOffsetAdj * FXAA311_SUBPIXEL_QUAL;

  if (DEBUG_SUBPIXEL_W)
  {
    return FsOutput(encode_aa_result(float3(subpixelOffsetAdj + 0.2f), constants));
  }

  if (DEBUG_SOLID_COLOR)
  {
    return FsOutput(encode_aa_result(float3(0.9f, 0.9f, 0.9f), constants));
  }

  let combinedOffset = max(finalOff, subpixelOffset);
  let finalTc = texCoord + offBase * constants.mainTargetInverseResolution * combinedOffset;
  let finalCol = ldrImage.SampleLevel(finalTc, 0).xyz;

  return FsOutput(encode_aa_result(finalCol, constants));
}
