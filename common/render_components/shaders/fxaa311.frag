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

void main(void)
{
  const vec3 ccol = textureLod(ldrImage, surf.texCoord, 0.f).xyz;
  const float cluma = fxaa_luma(ccol);

  const vec3 ncol = textureLodOffset(ldrImage, subf.texCoord, 0.f, ivec2(-1, 0));
  const vec3 scol = textureLodOffset(ldrImage, subf.texCoord, 0.f, ivec2(1, 0));
  const vec3 wcol = textureLodOffset(ldrImage, subf.texCoord, 0.f, ivec2(0, -1));
  const vec3 ecol = textureLodOffset(ldrImage, subf.texCoord, 0.f, ivec2(0, 1));

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

  const vec3 nwcol = textureLodOffset(ldrImage, subf.texCoord, 0.f, ivec2(-1, -1));
  const vec3 swcol = textureLodOffset(ldrImage, subf.texCoord, 0.f, ivec2(1, -1));
  const vec3 necol = textureLodOffset(ldrImage, subf.texCoord, 0.f, ivec2(-1, 1));
  const vec3 secol = textureLodOffset(ldrImage, subf.texCoord, 0.f, ivec2(1, 1));

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

  

  // @TEMP
  vec3 finalCol = ccol;
  out_fragColor = vec4(ENCODE_AA_RESULT(finalCol), 1.f);
}


