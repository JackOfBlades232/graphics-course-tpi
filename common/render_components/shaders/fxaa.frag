#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "constants.h"

layout(location = 0) out vec4 out_fragColor;

layout(binding = 0) uniform sampler2D ldrImage;

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

#include "fxaa_common.frag.inc"

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

const float fxaa_edge_threshold_min = 1.f / 16.f;
const float fxaa_edge_threshold = 1.f / 8.f;
const uint fxaa_search_steps = 16;
const float fxaa_subpixel_trim = 0.5f;
const float fxaa_subpixel_trim_scale = 0.5f;
const float fxaa_subpixel_cap = 0.75f;

#define DEBUG_CUTOFF_PIXELS 0
#define DEBUG_SOLID_COLOR 0
#define DEBUG_EDGE_DIRECTION 0
#define DEBUG_SUBPIXEL_W 0

void main(void)
{
  const vec2 tc = surf.texCoord;

  const vec3 ccol = textureLod(ldrImage, tc, 0.f).xyz;
  const float cluma = fxaa_luma(ccol);

  const vec3 ncol = textureLodOffset(ldrImage, tc, 0, ivec2(0, -1)).xyz;
  const vec3 scol = textureLodOffset(ldrImage, tc, 0, ivec2(0, 1)).xyz;
  const vec3 wcol = textureLodOffset(ldrImage, tc, 0, ivec2(-1, 0)).xyz;
  const vec3 ecol = textureLodOffset(ldrImage, tc, 0, ivec2(1, 0)).xyz;

  const float nluma = fxaa_luma(ncol);
  const float sluma = fxaa_luma(scol);
  const float wluma = fxaa_luma(wcol);
  const float eluma = fxaa_luma(ecol);

  const float lumaMin = min(cluma, min(min(nluma, sluma), min(wluma, eluma)));
  const float lumaMax = max(cluma, max(max(nluma, sluma), max(wluma, eluma)));
  const float lumaRng = lumaMax - lumaMin;

  if (lumaRng < max(fxaa_edge_threshold_min, lumaMax * fxaa_edge_threshold))
  {
    out_fragColor = vec4(ENCODE_AA_RESULT(ccol), 1.f);

#if DEBUG_CUTOFF_PIXELS
    out_fragColor = vec4(ENCODE_AA_RESULT(vec3(0.2f, 0.2f, 0.2f)), 1.f);
#endif

    return;
  }

  const vec3 nwcol = textureLodOffset(ldrImage, tc, 0, ivec2(-1, -1)).xyz;
  const vec3 swcol = textureLodOffset(ldrImage, tc, 0, ivec2(-1, 1)).xyz;
  const vec3 necol = textureLodOffset(ldrImage, tc, 0, ivec2(1, -1)).xyz;
  const vec3 secol = textureLodOffset(ldrImage, tc, 0, ivec2(1, 1)).xyz;

  const float nwluma = fxaa_luma(nwcol);
  const float swluma = fxaa_luma(swcol);
  const float neluma = fxaa_luma(necol);
  const float seluma = fxaa_luma(secol);

  // edge
  const float edgeVert =
    fxaa_high_pass_filter(nwluma, nluma, neluma) +
    2.f * fxaa_high_pass_filter(wluma, cluma, eluma) +
    fxaa_high_pass_filter(swluma, sluma, seluma);
  const float edgeHoriz =
    fxaa_high_pass_filter(swluma, wluma, nwluma) +
    2.f * fxaa_high_pass_filter(sluma, cluma, nluma) +
    fxaa_high_pass_filter(seluma, eluma, neluma);

  const bool isHoriz = edgeHoriz >= edgeVert;

#if DEBUG_EDGE_DIRECTION
  if (isHoriz)
    out_fragColor = vec4(ENCODE_AA_RESULT(vec3(0.9f, 0.f, 0.1f)), 1.f);
  else
    out_fragColor = vec4(ENCODE_AA_RESULT(vec3(0.f, 0.9f, 0.1f)), 1.f);
  return;
#endif

  const float neiLuma1 = isHoriz ? nluma : wluma;
  const float neiLuma2 = isHoriz ? sluma : eluma;

  const float neiGrad1 = neiLuma1 - cluma;
  const float neiGrad2 = neiLuma2 - cluma;

  const bool nei1IsHigherContrast = abs(neiGrad1) >= abs(neiGrad2);

  const float hierNeiGrad = nei1IsHigherContrast ? neiGrad1 : neiGrad2;
  const float hierNeiLuma = nei1IsHigherContrast ? neiLuma1 : neiLuma2;

  const float gradCutoff = 0.25f * abs(hierNeiGrad);
  const float lumaLocalAv = 0.5f * (cluma + hierNeiLuma);

  const vec2 offBase =
    (nei1IsHigherContrast ? -1.f : 1.f) *
    (isHoriz ? vec2(0.f, 1.f) : vec2(1.f, 0.f));
  const vec2 off = 0.5f * constants.mainTargetInverseResolution * offBase;
  const vec2 stpBase = (isHoriz ? vec2(1.f, 0.f) : vec2(0.f, 1.f));
  const vec2 stp = constants.mainTargetInverseResolution * stpBase;

  bool doneP = false;
  bool doneN = false;

  float lumaEndP;
  float lumaEndN;

  vec2 offP = off - stp;
  vec2 offN = off + stp;

  for (uint i = 0; i < fxaa_search_steps; ++i)
  {
    if (!doneP)
      lumaEndP = fxaa_luma(textureLod(ldrImage, tc + offP, 0).xyz);
    if (!doneN)
      lumaEndN = fxaa_luma(textureLod(ldrImage, tc + offN, 0).xyz);
    doneP = doneP || (abs(lumaEndP - lumaLocalAv) >= gradCutoff);
    doneN = doneN || (abs(lumaEndN - lumaLocalAv) >= gradCutoff);
    if (doneP && doneN)
      break;
    if (!doneP)
      offP -= stp;
    if (!doneN)
      offN += stp;
  }

  const float dP = isHoriz ? -offP.x : -offP.y;
  const float dN = isHoriz ? offN.x : offN.y;

  const bool pIsClosest = dP < dN;
  const float closestD = min(dP, dN);
  const float edgeW = dP + dN;
  const float closestLuma = pIsClosest ? lumaEndP : lumaEndN;

  const float sampleOffsetAlongEdge = 0.5f - closestD / edgeW;

  const bool lumaCIsSmaller = cluma < lumaLocalAv;
  const bool edgeEndIsSameAsNei = lumaCIsSmaller ? (closestLuma > lumaLocalAv) : (closestLuma < lumaLocalAv);

  const vec2 sampleTc =
    tc + (edgeEndIsSameAsNei ? (offBase * constants.mainTargetInverseResolution * sampleOffsetAlongEdge) : vec2(0.f));

  const vec3 edgeAntialiasedCol = textureLod(ldrImage, sampleTc, 0).xyz;

  // subpixel
  const float lowpassLuma = (nluma + sluma + wluma + eluma) * 0.25f;
  const float lowpassRange = abs(lowpassLuma - cluma);
  const float subpixelBlend =
    min(
      max(0.f, (lowpassRange / lumaRng) - fxaa_subpixel_trim) * fxaa_subpixel_trim_scale,
      fxaa_subpixel_cap);

#if DEBUG_SUBPIXEL_W
  out_fragColor = vec4(ENCODE_AA_RESULT(vec3(subpixelBlend + 0.2f)), 1.f);
  return;
#endif

  const vec3 rgbLowpassFull =
    (ccol + ncol + scol + wcol + ecol + nwcol + swcol + necol + secol) * (1.f / 9.f);

  const vec3 finalCol = mix(edgeAntialiasedCol, rgbLowpassFull, subpixelBlend);
  out_fragColor = vec4(ENCODE_AA_RESULT(finalCol), 1.f);

#if DEBUG_SOLID_COLOR
  out_fragColor = vec4(ENCODE_AA_RESULT(vec3(0.9f, 0.9f, 0.9f)), 1.f);
#endif
}

