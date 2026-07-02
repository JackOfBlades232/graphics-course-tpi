#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "constants.h"

layout(location = 0) out vec4 out_fragColor;
layout(location = 1) out vec4 out_linearColor;

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

// from https://advances.realtimerendering.com/s2016/Filmic%20SMAA%20v7.pptx , p 92
// @TODO same as with 9tap
vec4 sample_tex_catmull_rom_5tap(in sampler2D texSmp, in vec2 uv, in vec2 texSize)
{
  vec4 rtMetrics = vec4(1.0 / texSize.xy, texSize.xy);

  vec2 position = rtMetrics.zw * uv;
  vec2 centerPosition = floor(position - 0.5) + 0.5;
  vec2 f = position - centerPosition;
  vec2 f2 = f * f;
  vec2 f3 = f * f2;


  const float c = 0.4; // note: [0;1] ( SMAA_FILMIC_REPROJECTION_SHARPNESS / 100.0 )
  vec2 w0 = -c * f3 + 2.0 * c * f2 - c * f;
  vec2 w1 = (2.0 - c) * f3 - (3.0 - c) * f2 + 1.0;
  vec2 w2 = -(2.0 - c) * f3 + (3.0 - 2.0 * c) * f2 + c * f;
  vec2 w3 = c * f3 - c * f2;

  vec2 w12 = w1 + w2;
  vec2 tc12 = rtMetrics.xy * (centerPosition + w2 / w12);
  vec3 centerColor = textureLod(texSmp, vec2(tc12.x, tc12.y), 0).rgb;

  vec2 tc0 = rtMetrics.xy * (centerPosition - 1.0);
  vec2 tc3 = rtMetrics.xy * (centerPosition + 2.0);
  vec4 color =
    vec4(textureLod(texSmp, vec2(tc12.x, tc0.y), 0).rgb, 1.0) * (w12.x * w0.y) +
    vec4(textureLod(texSmp, vec2(tc0.x, tc12.y), 0).rgb, 1.0) * (w0.x * w12.y) +
    vec4(centerColor, 1.0) * (w12.x * w12.y) +
    vec4(textureLod(texSmp, vec2(tc3.x, tc12.y), 0).rgb, 1.0) * (w3.x * w12.y) +
    vec4(textureLod(texSmp, vec2(tc12.x, tc3.y), 0).rgb, 1.0) * (w12.x * w3.y);
  return vec4(color.rgb / color.a, 1.0);
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

const mat3 MITCHELL_3X3 = mat3(
  0.00308642,
  0.04938272,
  0.00308642,
  0.04938272,
  0.79012346,
  0.04938272,
  0.00308642,
  0.04938272,
  0.00308642);

struct NeighbourhoodData
{
  vec3 sourceSample;
  vec3 clipBoxMin;
  vec3 clipBoxMax;
  vec3 clampBoxMin;
  vec3 clampBoxMax;
  vec2 dilatedMotionVector;
  float convergenceFactor;
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
  nd.sourceSample = vec3(0.f);
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
      float w = MITCHELL_3X3[x + 1][y + 1];
      nd.sourceSample += nc * w;
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
  nd.convergenceFactor = textureLod(motionVectors, uv, 0).z;
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

void main(void)
{
  NeighbourhoodData neiData = sample_neighbourhood(surf.texCoord, viewParams);
  vec3 curCol = neiData.sourceSample;
  vec3 finalCol = curCol;

  if (constants.frameNo > 0)
  {
    vec2 uv = surf.texCoord - neiData.dilatedMotionVector;
    if (uv.x >= 0.f && uv.x <= 1.f && uv.y >= 0.f && uv.y <= 1.f)
    {
      vec3 prevCol = sample_tex_catmull_rom_5tap(prevFrame, uv, textureSize(prevFrame, 0)).xyz;

      prevCol = clamp(prevCol, neiData.clampBoxMin, neiData.clampBoxMax);
      prevCol = clip_to_aabb_center(prevCol, neiData.clipBoxMin, neiData.clipBoxMax);

      float sourceW = neiData.convergenceFactor;
      float histW = 1.f - sourceW;
      vec3 curCompressed = curCol / (max(curCol.x, max(curCol.y, curCol.z)) + 1.f);
      vec3 prevCompressed = prevCol / (max(prevCol.x, max(prevCol.y, prevCol.z)) + 1.f);
      float curLuminance = ldr_luminance(curCompressed);
      float prevLuminance = ldr_luminance(prevCompressed);
      sourceW /= curLuminance + 1.f;
      histW /= prevLuminance + 1.f;

      finalCol = (curCol * sourceW + prevCol * histW) / max(sourceW + histW, 0.0001f);
    }
  }

  out_linearColor = vec4(finalCol, 1.f);
  out_fragColor = vec4(ENCODE_AA_RESULT(finalCol), 1.f);
}
