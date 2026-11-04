import postfx_vs;
import constants;
import common;

uniform let constants: ConstantBuffer<Constants>;
uniform let viewParams: ConstantBuffer<ViewParams>;

struct GenerationFsOutput
{
  var packed: float4 : SV_Target0;
};

struct BlurFsOutput
{
  var ao: float : SV_Target0;
};

struct GenerationOutput
{
  float ao = 0.f;
  float worldDepth = 0.f;
  uint validHistory = 0;
  float normalizedKernelCull = 0.f;
};

func pack_generation_output(GenerationOutput unpacked) -> float4
{
  var packed: float4;
  packed.x = unpacked.ao;
  packed.y = unpacked.worldDepth;
  packed.z = float(unpacked.validHistory);
  packed.w = unpacked.normalizedKernelCull;
  return packed;
}

func unpack_generation_output(float4 packed) -> GenerationOutput
{
  return GenerationOutput(packed.x, packed.y, uint(packed.z), packed.w);
}

func recover_view_dist(
  ndc_uv: float2, z: float, inv_aspect: float2, params: ViewParams) -> float
{
  let linz = linearize_rev_z(z, params.viewFrustum.nearZ, params.viewFrustum.farZ);
  let ray = ndc_uv * inv_aspect;
  let raylen = sqrt(dot(ray, ray) + 1.f);
  return raylen * linz;
}

[shader("fragment")]
func ssao_generate(
  texCoord: float2 : TEXCOORD,
  gbufDepth: Sampler2D<float>,
  gbufNormal: Sampler2D<float3>,
  prevFrame: Sampler2D<float4>,
  motionVectors: Sampler2D<float2>) -> GenerationFsOutput
{
  let depth = max(gbufDepth.SampleLevel(texCoord, 0), 0.f);
  if (depth <= 0.f)
    return GenerationFsOutput(float4(1.f, depth, 0.f, 0.f));

  var output: GenerationOutput = {};

  let scrsz = gbufDepth.getSize();
  let pixcoord = texCoord * float2(scrsz);
  let rotcoord = uint2(floor(pixcoord)) % uint2(SSAO_BLUR_KERNEL_SIZE);
  let lincoord = (rotcoord.x + rotcoord.y * SSAO_BLUR_KERNEL_SIZE) % SSAO_BLUR_KERNEL_TSIZE;
  let v4coord = lincoord >> 1;
  let comp = lincoord & 1;
  let rr = constants.ssaoData.ssaoKernelRotations[v4coord];
  let rot = comp == 0 ? rr.xy : rr.zw;

  // @TODO: report matrix addressing bug to slang
  let invaspect = float2(
    1.f / viewParams.mProj[0][0],
    1.f / viewParams.mProj[1][1]);

  let reconstructedPos = depth_and_tc_to_pos(depth, texCoord, viewParams.mInverseProjView);
  let normal = gbufNormal.SampleLevel(texCoord, 0);

  output.worldDepth = length(reconstructedPos - viewParams.viewPos);

  let fovYW = output.worldDepth / abs(viewParams.mProj[1][1]);

  let maxKernToFovy = 0.75f;
  let minKernToFovy = 0.05f;
  let closeKernelScalingFactor = maxKernToFovy * fovYW;
  let farKernelScalingFactor = minKernToFovy * fovYW;
  let kernelScalingFactor = min(closeKernelScalingFactor, 1.f) * max(farKernelScalingFactor, 1.f);
  let rad = kernelScalingFactor * constants.ssaoRadius;

  let ssradY = rad / fovYW;
  let ssradX = ssradY * abs(viewParams.mProj[0][0]) / abs(viewParams.mProj[1][1]);

  let dte = min(texCoord, 1.f - texCoord);
  let kernelCull = clamp(float2(dte.x / ssradX, dte.y / ssradY), 0.f, 1.f);

  output.normalizedKernelCull = min(kernelCull.x, kernelCull.y);

  let baseVec = float3(rot, 0.f);
  let tangent = normalize(baseVec - normal * dot(baseVec, normal));
  let bitangent = cross(normal, tangent);
  let tbnTransform = float3x3(tangent, bitangent, normal);

  let allSamples = min(constants.ssaoLimitSamples, SSAO_KERNEL_MAX_SIZE);
  var samples: uint = allSamples;
  var frameId: uint = 0;
  if (constants.ssaoDoTemporalAccum)
  {
    samples /= constants.ssaoTemporalAccumBacklog;
    frameId = constants.frameNo % constants.ssaoTemporalAccumBacklog;
  }

  var prevOcc: float = 0.f;
  var prevOccW: float = 0.f;
  if (constants.ssaoDoTemporalAccum && !constants.ssaoForceDropHistory && constants.frameNo > 0)
  {
    let motion = motionVectors.SampleLevel(texCoord, 0);
    let uv = texCoord - motion;
    if (uv.x >= 0.f && uv.x <= 1.f && uv.y >= 0.f && uv.y <= 1.f)
    {
      GenerationOutput prev = unpack_generation_output(prevFrame.SampleLevel(uv, 0));

      let ppViewDepth = length(reconstructedPos - viewParams.prevViewPos);

      let disocclusionParam = abs(1.f - ppViewDepth / prev.worldDepth);
      float kernelCullDisocclusion =
        max(output.normalizedKernelCull - prev.normalizedKernelCull, 0.f);

      if (disocclusionParam < constants.ssaoDepthRejectionThreshold && kernelCullDisocclusion < 0.01f)
      {
        prevOcc = prev.ao;
        output.validHistory = uint(prev.validHistory) + 1;
        prevOccW = 1.f - max(constants.ssaoEmaCoeff, 1.f / float(output.validHistory));
      }
    }
  }

  var firstSample: uint = frameId * samples;
  var capSample: uint = firstSample + samples;

  // If discarding cache, in conservative mode dynamically accelerate conversion
  if (constants.ssaoDoTemporalAccum && constants.ssaoConservativeTemporalCaching)
  {
    if (output.validHistory < 2)
    {
      firstSample = 0;
      capSample = allSamples;
    }
    else if (constants.ssaoTemporalAccumBacklog > 2 && output.validHistory < 4)
    {
      firstSample = output.validHistory - 2;
      capSample = firstSample + allSamples / 2;
    }
    else if (constants.ssaoTemporalAccumBacklog > 4 && output.validHistory < 8)
    {
      firstSample = output.validHistory - 4;
      capSample = firstSample + allSamples / 4;
    }
  }

  for (var i: uint = firstSample; i < capSample; ++i)
  {
    let sampleWorldPos = reconstructedPos + rad * mul(constants.ssaoData.ssaoKernel[i].xyz, tbnTransform);
    let sampleClipPosW = mul(float4(sampleWorldPos, 1.f), viewParams.mProjView);
    let sampleClipPos = sampleClipPosW.xyz / sampleClipPosW.w;
    let sampleTc = sampleClipPos.xy * 0.5f + 0.5f;
    if (sampleTc.x >= 0.f && sampleTc.x <= 1.f && sampleTc.y >= 0.f && sampleTc.y <= 1.f)
    {
      let sampleDepth = gbufDepth.SampleLevel(sampleTc, 0);

      let sampleWorldDepth = length(sampleWorldPos - viewParams.viewPos);
      let sampleGbufDepth = recover_view_dist(
        sampleClipPos.xy, sampleDepth, invaspect, viewParams);

      let rangeCutoff = smoothstep(0.f, 1.f, rad / abs(output.worldDepth - sampleGbufDepth));
      output.ao += (sampleWorldDepth >= sampleGbufDepth + constants.ssaoBias ? 1.f : 0.f) * rangeCutoff;
    }
  }

  output.ao = 1.f - output.ao / float(capSample - firstSample);
  output.ao = output.ao * (1.f - prevOccW) + prevOcc * prevOccW;

  return GenerationFsOutput(pack_generation_output(output));
}

[shader("fragment")]
func ssao_blur(
  texCoord: float2 : TEXCOORD,
  input: Sampler2D<float4>) -> BlurFsOutput
{
  let centerDepth = input.SampleLevel(texCoord, 0).y;
  var result: float = 0.f;
  var tw: float = 0.f;
  for (var x: int = -SSAO_BLUR_KERNEL_HS; x <= SSAO_BLUR_KERNEL_HS; ++x) 
  {
    for (var y: int = -SSAO_BLUR_KERNEL_HS; y <= SSAO_BLUR_KERNEL_HS; ++y) 
    {
      let data = unpack_generation_output(input.SampleLevel(texCoord, 0, int2(x, y)));
      float dd = abs(centerDepth - data.worldDepth);
      float dw = max(0.f, 1.f - dd / (2.f * constants.ssaoRadius)); // 0 when kernels do not intersect
      result += data.ao * dw;
      tw += dw;
    }
  }
  var finalAo: float = result / tw;
  if (abs(constants.ssaoPower - 1.f) > EPSILON)
    finalAo = pow(finalAo, constants.ssaoPower);
  return BlurFsOutput(finalAo);
}

