#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "constants.h"

layout(location = 0) out vec4 out_fragColor;

layout(binding = 0) uniform sampler2D ldrImage;

layout(binding = 1) uniform sampler2D prevFrame;
layout(binding = 2) uniform sampler2D motionVectors;
layout(binding = 3) uniform sampler2D gbufDepth;

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};
layout(binding = 9, set = 0) uniform view_params_t
{
  ViewParams viewParams;
};

#include "tonemap.frag.inc"
#include "common.frag.inc"

layout(location = 0) in VS_OUT
{
  vec2 texCoord;
}
surf;

// Thanks https://gist.github.com/TheRealMJP/bc503b0b87b643d3505d41eab8b332ae
// @TODO: study
// @TODO: replace all with this
// @TODO: 5 taps
vec4 sample_tex_catmull_rom_9tap(in sampler2D texSmp, in vec2 uv, in vec2 texSize)
{
  vec2 samplePos = uv * texSize;
  vec2 texPos1 = floor(samplePos - 0.5f) + 0.5f;

  // Compute the fractional offset from our starting texel to our original sample location, which
  // we'll feed into the Catmull-Rom spline function to get our filter weights.
  vec2 f = samplePos - texPos1;

  // Compute the Catmull-Rom weights using the fractional offset that we calculated earlier.
  // These equations are pre-expanded based on our knowledge of where the texels will be located,
  // which lets us avoid having to evaluate a piece-wise function.
  vec2 w0 = f * (-0.5f + f * (1.0f - 0.5f * f));
  vec2 w1 = 1.0f + f * f * (-2.5f + 1.5f * f);
  vec2 w2 = f * (0.5f + f * (2.0f - 1.5f * f));
  vec2 w3 = f * f * (-0.5f + 0.5f * f);

  // Work out weighting factors and sampling offsets that will let us use bilinear filtering to
  // simultaneously evaluate the middle 2 samples from the 4x4 grid.
  vec2 w12 = w1 + w2;
  vec2 offset12 = w2 / (w1 + w2);

  // Compute the final UV coordinates we'll use for sampling the texture
  vec2 texPos0 = texPos1 - 1;
  vec2 texPos3 = texPos1 + 2;
  vec2 texPos12 = texPos1 + offset12;

  texPos0 /= texSize;
  texPos3 /= texSize;
  texPos12 /= texSize;

  vec4 result = vec4(0.0f);
  result += textureLod(texSmp, vec2(texPos0.x, texPos0.y), 0.0f) * w0.x * w0.y;
  result += textureLod(texSmp, vec2(texPos12.x, texPos0.y), 0.0f) * w12.x * w0.y;
  result += textureLod(texSmp, vec2(texPos3.x, texPos0.y), 0.0f) * w3.x * w0.y;

  result += textureLod(texSmp, vec2(texPos0.x, texPos12.y), 0.0f) * w0.x * w12.y;
  result += textureLod(texSmp, vec2(texPos12.x, texPos12.y), 0.0f) * w12.x * w12.y;
  result += textureLod(texSmp, vec2(texPos3.x, texPos12.y), 0.0f) * w3.x * w12.y;

  result += textureLod(texSmp, vec2(texPos0.x, texPos3.y), 0.0f) * w0.x * w3.y;
  result += textureLod(texSmp, vec2(texPos12.x, texPos3.y), 0.0f) * w12.x * w3.y;
  result += textureLod(texSmp, vec2(texPos3.x, texPos3.y), 0.0f) * w3.x * w3.y;

  return result;
}

float closest_depth_init(in ViewParams params)
{
  if (params.needReverseZ != 0)
    return -9999.f;
  else
    return 9999.f;
}

bool closest_depth_compare(float closest, float new, in ViewParams params)
{
  return params.needReverseZ != 0 ? new > closest : new < closest;
}

struct NeighbourhoodData
{
  vec3 clipBoxMin;
  vec3 clipBoxMax;
  vec3 clampBoxMin;
  vec3 clampBoxMax;
  vec2 dilatedMotionVector;
  float closestDepth;
};

NeighbourhoodData sample_neighbourhood(vec2 uv, in ViewParams params)
{
  NeighbourhoodData nd;
  vec3 minCol = vec3(9999.f);
  vec3 maxCol = vec3(-9999.f);
  vec3 m1 = vec3(0.f);
  vec3 m2 = vec3(0.f);
  nd.closestDepth = closest_depth_init(params);
  vec2 closestDepthUv = uv;
  for (int x = -1; x <= 1; ++x)
    for (int y = -1; y <= 1; ++y)
    {
      vec2 neiUv = uv + vec2(x, y) * constants.mainTargetInverseResolution;
      neiUv = clamp(neiUv, 0.f, 1.f);
      vec3 nc = textureLod(ldrImage, neiUv, 0).xyz;
      minCol = min(minCol, nc);
      maxCol = max(maxCol, nc);
      m1 += nc;
      m2 += nc * nc;
      float d = textureLod(gbufDepth, neiUv, 0).x;
      if (closest_depth_compare(nd.closestDepth, d, params))
      {
        nd.closestDepth = d;
        closestDepthUv = neiUv;
      }
    }
  vec3 mu = m1 / 9.f;
  vec3 sigma = sqrt(abs((m2 / 9.f) - (mu * mu)));
  const float gamma = 1.f;
  nd.dilatedMotionVector = textureLod(motionVectors, closestDepthUv, 0).xy;
  nd.clampBoxMin = minCol;
  nd.clampBoxMax = maxCol;
  nd.clipBoxMin = mu - gamma * sigma;
  nd.clipBoxMax = mu + gamma * sigma;
  return nd;
}

vec3 clip_to_aabb_center(vec3 pnt, vec3 bbmin, vec3 bbmax)
{
  vec3 center = 0.5f * (bbmax + bbmin);
  vec3 extent = 0.5f * (bbmax - bbmin);
  vec3 v = pnt - center;
  vec3 vn = v / extent;
  vec3 avn = abs(vn);
  float maxNormOff = max(avn.x, max(avn.y, avn.z));
  if (maxNormOff > 1.f)
    return center + v / maxNormOff;
  else
    return pnt; // inside
}

float ldr_luminance(vec3 col)
{
  return dot(col, vec3(0.2127f, 0.7152f, 0.0722f));
}

vec2 unjitter_screen_uv(vec2 suv, in ViewParams params)
{
  return clamp(suv - get_subpixel_uv_jitter(params) * constants.mainTargetInverseResolution, 0.f, 1.f);
}

void main(void)
{
  vec3 curCol = textureLod(ldrImage, unjitter_screen_uv(surf.texCoord, viewParams), 0.f).xyz;
  vec3 finalCol = curCol;

  if (constants.frameNo > 0)
  {
    NeighbourhoodData neiData = sample_neighbourhood(surf.texCoord, viewParams);
    vec2 uv = surf.texCoord - neiData.dilatedMotionVector;
    if (uv.x >= 0.f && uv.x <= 1.f && uv.y >= 0.f && uv.y <= 1.f)
    {
      vec3 prevCol = sample_tex_catmull_rom_9tap(prevFrame, uv, textureSize(prevFrame, 0)).xyz;
      prevCol = clamp(prevCol, neiData.clampBoxMin, neiData.clampBoxMax);
      prevCol = clip_to_aabb_center(prevCol, neiData.clipBoxMin, neiData.clipBoxMax);
      float sourceW = constants.taaEmaCoeff;
      float histW = 1.f - sourceW;
      sourceW /= ldr_luminance(curCol) + 1.f;
      histW /= ldr_luminance(prevCol) + 1.f;
      finalCol = (curCol * sourceW + prevCol * histW) / max(sourceW + histW, 0.0001f);
    }
  }

  // No gamma encoding here -- we need linear for history
  out_fragColor = vec4(finalCol, 1.f);
}
