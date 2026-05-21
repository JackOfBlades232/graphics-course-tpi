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

void main(void)
{
  const vec2 tc = surf.texCoord;

  const vec3 ccol = textureLod(ldrImage, tc, 0.f).xyz;
  const float cluma = fxaa_luma(ccol);

  const vec3 ncol = textureLodOffset(ldrImage, tc, 0.f, ivec2(-1, 0));
  const vec3 scol = textureLodOffset(ldrImage, tc, 0.f, ivec2(1, 0));
  const vec3 wcol = textureLodOffset(ldrImage, tc, 0.f, ivec2(0, -1));
  const vec3 ecol = textureLodOffset(ldrImage, tc, 0.f, ivec2(0, 1));

  const float nluma = fxaa_luma(ncol);
  const float sluma = fxaa_luma(scol);
  const float wluma = fxaa_luma(wcol);
  const float eluma = fxaa_luma(ecol);

  const float lumaMin = min(ccol, min(min(nluma, sluma), min(wluma, eluma)));
  const float lumaMax = max(ccol, min(max(nluma, sluma), max(wluma, eluma)));
  const float lumaRng = lumaMin - lumaMax;

  if (lumaRng < max(fxaa_edge_threshold_min, lumaMax * fxaa_edge_threshold))
  {
    out_fragColor = vec4(ENCODE_AA_RESULT(ccol), 1.f);
    return;
  }

  const vec3 nwcol = textureLodOffset(ldrImage, tc, 0.f, ivec2(-1, -1));
  const vec3 swcol = textureLodOffset(ldrImage, tc, 0.f, ivec2(1, -1));
  const vec3 necol = textureLodOffset(ldrImage, tc, 0.f, ivec2(-1, 1));
  const vec3 secol = textureLodOffset(ldrImage, tc, 0.f, ivec2(1, 1));

  const float nwluma = fxaa_luma(nwcol);
  const float swluma = fxaa_luma(swcol);
  const float neluma = fxaa_luma(necol);
  const float seluma = fxaa_luma(secol);

  const float edgeVert =
    fxaa_high_pass_filter(nwluma, nluma, neluma) +
    2.f * fxaa_high_pass_filter(wluma, cluma, eluma) +
    fxaa_high_pass_filter(swluma, sluma, seluma);
  const float edgeHoriz =
    fxaa_high_pass_filter(swluma, wluma, nwluma) +
    2.f * fxaa_high_pass_filter(sluma, cluma, nluma) +
    fxaa_high_pass_filter(seluma, eluma, neluma);

  const bool isHoriz = edgeHoriz >= edgeVert;

  const float neiLuma1 = isHoriz ? nluma : wluma;
  const float neiLuma2 = isHoriz ? sluma : eluma;

  const float neiGrad1 = neiLuma1 - cluma;
  const float neiGrad2 = neiLuma2 - cluma;

  const bool nei1IsHigherContrast = abs(neiGrad1) >= abs(neiGrad2);

  const float hierNeiGrad = nei1IsHigherContrast ? neiGrad1 : neiGrad2;
  const float hierNeiLuma = nei1IsHigherContrast ? neiLuma1 : neiLuma2;

  const vec2 offBase =
    (nei1IsHigherContrast ? -1.f : 1.f) *
    (isHoriz ? vec2(0.f, 1.f) : vec2(1.f, 0.f));
  const vec2 off = 0.5f * constants.mainTargetInverseResolution * offBase;
  const vec2 stpBase = (isHoriz ? vec2(1.f, 0.f) : vec2(0.f, 1.f));
  const vec2 stp = constants.mainTargetInverseResolution * stpBase;

  const bool doneP = false;
  const bool doneN = false;

  const float lumaEndP;
  const float lumaEndN;

  const vec2 offP = off - stp;
  const vec2 offN = off + stp;

  for (uint i = 0; i < fxaa_search_steps; ++i)
  {
    if (!doneP)
      lumaEndP = fxaa_luma(textureLod(ldrImage, tc + offP).xyz);
    if (!doneN)
      lumaEndN = fxaa_luma(textureLod(ldrImage, tc + offN).xyz);
    doneP = doneP || (abs(lumaEndP - hierNeiLuma) >= hierNeiGrad);
    doneN = doneN || (abs(lumaEndN - hierNeiLuma) >= hierNeiGrad);
    if (doneP && doneN)
      break;
    if (!doneP)
      offP -= stp;
    if (!doneN)
      offN += stp;
  }

  const float dP = isHoriz ? -offP.x : -offP.y;
  const float dN = isHoriz ? offN.x : offN.y;

  const float pIsClosest = dP < dN;
  const float closestD = min(dP, dN);
  const float edgeW = dP + dN;

  const float sampleOffsetAlongEdge = 0.5f - closestD / edgeW;
  const vec2 sampleTc = tc + offBase * constants.mainTargetInverseResolution * sampleOffsetAlongEdge;


  // @TODO: subpixel antialias
  // @TODO: fix and tune this

  vec3 finalCol = textureLod(ldrTarget, sampleTc).xyz;
  out_fragColor = vec4(ENCODE_AA_RESULT(finalCol), 1.f);
}

