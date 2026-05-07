#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : enable

#include "ssao.h"
#include "geometry.h"
#include "constants.h"

layout(location = 0) out vec2 out_ao;

layout(binding = 0, set = 0) uniform sampler2D gbufNormal;
layout(binding = 1, set = 0) uniform sampler2D gbufDepth;

layout(binding = 2, set = 0) uniform sampler2D prevFrameAo;

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};
layout(binding = 9, set = 0) uniform view_params_t
{
  ViewParams viewParams;
};
layout(binding = 10, set = 0) readonly buffer view_data_t
{
  ViewData viewData;
};

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

// @TODO: dedup, optimize
vec3 depth_and_tc_to_pos_with_mat(float depth, vec2 tc, mat4 ivpm)
{
  const vec4 cameraToScreen = vec4(2.f * tc - 1.f, depth, 1.f); 
  const vec4 posHom = ivpm * cameraToScreen;
  return posHom.xyz / posHom.w;
}

void main()
{
  const float depth = texture(gbufDepth, surf.texCoord).x;
  if (depth <= 0.f)
  {
    out_ao = vec2(1.f, depth);
    return;
  }

  const ivec2 scrsz = textureSize(gbufDepth, 0);
  const vec2 pixcoord = surf.texCoord * vec2(scrsz);
  const uvec2 rotcoord = (uvec2(floor(pixcoord)) + uvec2(constants.frameNo)) % uvec2(SSAO_BLUR_KERNEL_SIZE);
  const uint lincoord = (rotcoord.x + rotcoord.y * SSAO_BLUR_KERNEL_SIZE) % SSAO_BLUR_KERNEL_TSIZE;
  const uint v4coord = lincoord >> 1;
  const uint comp = lincoord & 1;
  const vec4 rr = constants.ssaoData.ssaoKernelRotations[v4coord];
  const vec2 rot = comp == 0 ? rr.xy : rr.zw;

  // piggy
  const mat4 vpm = calc_adjusted_viewproj_mat(viewParams, viewData);
  const mat4 ivpm = inverse(vpm);

  const vec3 reconstructedPos = depth_and_tc_to_pos_with_mat(max(depth, 0.f), surf.texCoord, ivpm);
  const vec3 normal = texture(gbufNormal, surf.texCoord).xyz;

  const float fragDepth = length(reconstructedPos - viewParams.viewPos);

  const float maxKernToFovy = 0.75f;
  const float kernelScalingFactor =
    min(fragDepth * maxKernToFovy / abs(viewParams.mProj[1][1]), 1.f);
  const float rad = kernelScalingFactor * constants.ssaoRadius;

  vec3 baseVec = vec3(rot, 0.f);
  vec3 tangent = normalize(baseVec - normal * dot(baseVec, normal));
  vec3 bitangent = cross(normal, tangent);
  mat3 tbnTransform = mat3(tangent, bitangent, normal);

  uint samples = min(constants.ssaoLimitSamples, SSAO_KERNEL_MAX_SIZE);
  uint frameId = 0;
  if (constants.ssaoDoTemporalAccum != 0)
  {
    samples /= constants.ssaoTemporalAccumBacklog;
    frameId = constants.frameNo % constants.ssaoTemporalAccumBacklog;
  }

  float occ = 0.f;
  float totw = 0.f;
  for (uint i = frameId * samples; i < (frameId + 1) * samples; ++i)
  {
    vec3 sampleWorldPos = reconstructedPos + rad * tbnTransform * constants.ssaoData.ssaoKernel[i].xyz;
    vec4 sampleClipPosW = vpm * vec4(sampleWorldPos, 1.f);
    vec3 sampleClipPos = sampleClipPosW.xyz / sampleClipPosW.w;
    vec2 sampleTc = sampleClipPos.xy * 0.5f + 0.5f;
    if (sampleTc.x >= 0.f && sampleTc.x <= 1.f && sampleTc.y >= 0.f && sampleTc.y <= 1.f)
    {
      float sampleDepth = texture(gbufDepth, sampleTc).x;
      vec3 sampleGbufPos = depth_and_tc_to_pos_with_mat(max(sampleDepth, 0.f), sampleTc, ivpm);

      float sampleWorldDepth = length(sampleWorldPos - viewParams.viewPos);
      float sampleGbufDepth = length(sampleGbufPos - viewParams.viewPos);

      float rangeCutoff = smoothstep(0.f, 1.f, rad / abs(fragDepth - sampleGbufDepth));
      occ += (sampleWorldDepth >= sampleGbufDepth + constants.ssaoBias ? 1.f : 0.f) * rangeCutoff;
      totw += 1.f;
    }
  }
  occ = 1.f - (totw == 0.f ? 0.f : occ / totw);
  if (constants.ssaoDoTemporalAccum != 0 && constants.ssaoForceDropHistory == 0 && constants.frameNo > 0)
  {
    vec4 prevUvz = calc_prev_adjusted_viewproj_mat(viewParams, viewData) * vec4(reconstructedPos, 1.f); 
    prevUvz /= prevUvz.w;
    vec2 uv = prevUvz.xy * 0.5f + 0.5f;
    if (uv.x >= 0.f && uv.x <= 1.f && uv.y >= 0.f && uv.y <= 1.f)
    {
      vec2 prevAoDepth = texture(prevFrameAo, uv).xy;

      float ppViewDepth = length(reconstructedPos - viewParams.prevViewPos);

      float disocclusionParam = abs(1.f - ppViewDepth / prevAoDepth.y);

      if (disocclusionParam < constants.ssaoDepthRejectionThreshold)
      {
        occ = constants.ssaoEmaCoeff * occ + (1.f - constants.ssaoEmaCoeff) * prevAoDepth.x;
      }
    }
  }
  out_ao = vec2(occ, fragDepth);
}
