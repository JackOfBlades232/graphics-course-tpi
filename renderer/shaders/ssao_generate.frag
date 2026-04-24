#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : enable

#include "ssao.h"
#include "geometry.h"
#include "constants.h"

layout(location = 0) out float out_ao;

layout(binding = 0, set = 0) uniform sampler2D gbufNormal;
layout(binding = 1, set = 0) uniform sampler2D gbufDepth;

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
    out_ao = 1.f;
    return;
  }

  const ivec2 scrsz = textureSize(gbufDepth, 0);
  const vec2 pixcoord = surf.texCoord * vec2(scrsz);
  const uvec2 rotcoord = uvec2(floor(pixcoord)) % uvec2(SSAO_BLUR_KERNEL_SIZE);
  const uint lincoord = rotcoord.x + rotcoord.y * SSAO_BLUR_KERNEL_SIZE;
  const uint v4coord = lincoord >> 1;
  const uint comp = lincoord & 1;
  const vec4 rr = constants.ssaoData.ssaoKernelRotations[v4coord];
  const vec2 rot = comp == 0 ? rr.xy : rr.zw;

  // piggy
  const mat4 vpm = calc_adjusted_viewproj_mat(viewParams, viewData);
  const mat4 ivpm = inverse(vpm);

  const vec3 reconstructedPos = depth_and_tc_to_pos_with_mat(max(depth, 0.f), surf.texCoord, ivpm);
  const vec3 normal = texture(gbufNormal, surf.texCoord).xyz;

  const float fragDepth = length(reconstructedPos - viewParams.mViewPos);

  vec3 baseVec = vec3(rot, 0.f);
  vec3 tangent = normalize(baseVec - normal * dot(baseVec, normal));
  vec3 bitangent = cross(normal, tangent);
  mat3 tbnTransform = mat3(tangent, bitangent, normal);

  float occ = 0.f;
  // @TODO: temporal accumulation option
  const uint samples = min(constants.ssaoLimitSamples, SSAO_KERNEL_MAX_SIZE);
  for (uint i = 0; i < samples; ++i)
  {
    vec3 sampleWorldPos = reconstructedPos + constants.ssaoRadius * tbnTransform * constants.ssaoData.ssaoKernel[i].xyz;
    vec4 sampleClipPosW = vpm * vec4(sampleWorldPos, 1.f);
    vec3 sampleClipPos = sampleClipPosW.xyz / sampleClipPosW.w;
    vec2 sampleTc = sampleClipPos.xy * 0.5f + 0.5f;
    float sampleDepth = texture(gbufDepth, sampleTc).x;
    vec3 sampleGbufPos = depth_and_tc_to_pos_with_mat(max(sampleDepth, 0.f), sampleTc, ivpm);

    float sampleWorldDepth = length(sampleWorldPos - viewParams.mViewPos);
    float sampleGbufDepth = length(sampleGbufPos - viewParams.mViewPos);

    float rangeCutoff = smoothstep(0.f, 1.f, constants.ssaoRadius / abs(fragDepth - sampleGbufDepth));
    occ += (sampleWorldDepth >= sampleGbufDepth + constants.ssaoBias ? 1.f : 0.f) * rangeCutoff;
  }
  occ = 1.f - (occ / float(constants.ssaoLimitSamples));
  if (abs(constants.ssaoPower - 1.f) > SHADER_EPSILON)
    occ = pow(occ, constants.ssaoPower);
  out_ao = occ;
}
