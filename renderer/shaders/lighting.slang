import constants;
import common;


// from https://github.com/TheRealMJP/Shadows
static const float PCF3X3_KERNEL[3][3] =
{
  { 0.5,1.0,0.5, },
  { 1.0,1.0,1.0, },
  { 0.5,1.0,0.5, }
};

static const float PCF5X5_KERNEL[5][5] =
{
  { 0.0,0.5,1.0,0.5,0.0 },
  { 0.5,1.0,1.0,1.0,0.5 },
  { 1.0,1.0,1.0,1.0,1.0 },
  { 0.5,1.0,1.0,1.0,0.5 },
  { 0.0,0.5,1.0,0.5,0.0 }
};

static const float PCF7X7_KERNEL[7][7] =
{
  { 0.0,0.0,0.5,1.0,0.5,0.0,0.0 },
  { 0.0,1.0,1.0,1.0,1.0,1.0,0.0 },
  { 0.5,1.0,1.0,1.0,1.0,1.0,0.5 },
  { 1.0,1.0,1.0,1.0,1.0,1.0,1.0 },
  { 0.5,1.0,1.0,1.0,1.0,1.0,0.5 },
  { 0.0,1.0,1.0,1.0,1.0,1.0,0.0 },
  { 0.0,0.0,0.5,1.0,0.5,0.0,0.0 }
};

static const float PCF9X9_KERNEL[9][9] =
{
  { 0.0,0.0,0.0,0.5,1.0,0.5,0.0,0.0,0.0 },
  { 0.0,0.0,1.0,1.0,1.0,1.0,1.0,0.0,0.0 },
  { 0.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.0 },
  { 0.5,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.5 },
  { 1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0 },
  { 0.5,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.5 },
  { 0.0,1.0,1.0,1.0,1.0,1.0,1.0,1.0,0.0 },
  { 0.0,0.0,1.0,1.0,1.0,1.0,1.0,0.0,0.0 },
  { 0.0,0.0,0.0,0.5,1.0,0.5,0.0,0.0,0.0 }
};

static const int PCF_KERNEL_SIZES[] = {0, 3, 5, 7, 9};

func pcf_kernel_weight(x: int, y: int, tech: ShadowTechnique) -> float
{
  switch (tech)
  {
  case ShadowTechnique.PCF3X3:
    return PCF3X3_KERNEL[y][x];
  case ShadowTechnique.PCF5X5:
    return PCF5X5_KERNEL[y][x];
  case ShadowTechnique.PCF7X7:
    return PCF7X7_KERNEL[y][x];
  case ShadowTechnique.PCF9X9:
    return PCF9X9_KERNEL[y][x];
  default:
    return 0.f;
  }
}

func calculate_attenuation(pos: float3, light_pos: float3, range: float) -> float
{
  let dist = length(pos - light_pos);
  return max(min(1.f - pow(dist / range, 4), 1.f), 0.f) / pow(dist, 2.f);
}

func calculate_angular_attenuation(
  cosine: float, inner_cos: float, outer_cos: float) -> float
{
  let angularAttenuation = clamp((cosine - inner_cos) / (outer_cos - inner_cos), 0.f, 1.f);
  return 1.f - pow(angularAttenuation, 2.5f);
}

struct CsmCascadeData
{
  var cascade: int;
  var zIntoCascadeStart: float;
  var zIntoCascadeEnd: float;
  var zCascadeSize: float;
  var zNextCascadeSize: float;
  var prevSplit: float;
  var split: float;
}

func get_cascade_data_for_view_pos(
  view_pos: float3, view_params: ViewParams) -> CsmCascadeData
{
  var data: CsmCascadeData;
  data.cascade = 0;
  data.zIntoCascadeStart = 0.f;
  data.zIntoCascadeEnd = 0.f;
  data.zCascadeSize = 0.f;
  data.prevSplit = 0.f;
  data.split = view_params.viewFrustum.nearZ;
  while (data.cascade < CSM_CASCADE_COUNT)
  {
    data.zIntoCascadeStart = view_pos.z - data.split;
    data.split = get_frustum_split(view_params, data.cascade);
    data.zIntoCascadeEnd = data.split - view_pos.z;
    data.zCascadeSize = data.split - data.prevSplit;

    if (view_pos.z <= data.split)
      break;

    data.prevSplit = data.split;

    ++data.cascade;
  }

  data.zNextCascadeSize = data.zCascadeSize;
  if (data.cascade < CSM_CASCADE_COUNT - 1)
    data.zNextCascadeSize = get_frustum_split(view_params, data.cascade + 1) - data.split;

  return data;
}

func get_csm_cascade_debug_multiplier(data: CsmCascadeData, constants: Constants) -> float4
{
  var debugMultiplier = float4(1.f);
  if (constants.drawCascadesInSolidColor)
  {
    let DEBUG_CASCADE_COLORS: float4[4] = {
      {1.f, 0.f, 0.f}, {0.f, 1.f, 0.f}, {0.f, 0.f, 1.f}, {0.f, 1.f, 1.f}};

    debugMultiplier = data.cascade == CSM_CASCADE_COUNT
      ? float4(1.f, 0.f, 1.f, 1.f)
      : DEBUG_CASCADE_COLORS[data.cascade & 3];
  }
  return debugMultiplier;
}

struct LightData
{
  var intensity: float3;
  var direction: float3;
  var shadow: float;
}

func calculate_csm_shadow_pcf_no_blend(
  lid: int, cid: int, pos: float3, tech: ShadowTechnique,
  lights: UniformLights, constants: Constants,
  mats: StructuredBuffer<LightMatrices>) -> float
{
  let posLightClipSpace = mul(float4(pos, 1.f), mats[0].directionalLightMats[lid][cid]);
  let posLightSpaceNDC = posLightClipSpace.xyz; // No perspective divide cuz ortho
  let shadowUv = posLightSpaceNDC.xy * 0.5f + 0.5f;

  var shadow: float =
    BindlessSampler2DShadow(lights.directionalLights[lid].shadowmapCascades[cid].map).SampleCmpLevelZero(shadowUv, posLightSpaceNDC.z);

  if (shadow_technique_is_pcf_kernel(tech))
  {
    let gridDim = PCF_KERNEL_SIZES[tech];
    let mid = gridDim / 2 + 1;

    let uvStep = float2(1.f / CSM_CASCADE_RESOLUTION);
    let uvBase = shadowUv - float(gridDim) * 0.5f * uvStep;

    var sampleCount: float = 1.f;

    for (var y: int = 0; y < gridDim; ++y)
      for (var x: int = 0; x < gridDim; ++x)
      {
        if (x == mid && y == mid)
          continue;

        let uv = uvBase + uvStep * float2(float(x), float(y));
        let w = pcf_kernel_weight(x, y, tech);

        shadow +=
          BindlessSampler2DShadow(lights.directionalLights[lid].shadowmapCascades[cid].map).SampleCmpLevelZero(uv, posLightSpaceNDC.z);
        sampleCount += w;
      }

    shadow /= sampleCount;
  }

  return shadow;
}

func calculate_csm_shadow_pcf(
  lid: int, cid: int, pos: float3,
  z_from_start: float, z_from_end: float, tech: ShadowTechnique,
  lights: UniformLights, constants: Constants,
  mats: StructuredBuffer<LightMatrices>) -> float
{
  let base = calculate_csm_shadow_pcf_no_blend(lid, cid, pos, tech, lights, constants, mats);
  var prev: float = 0.f;
  var next: float = 0.f;

  var basew: float = 1.f;
  var prevw: float = 0.f;
  var nextw: float = 0.f;

  // @TODO: non-linear blend without more samplings (sigmoid?)
  if (cid > 0 && z_from_start <= constants.csmBlendingBeltSize)
  {
    prevw = 0.5f * (constants.csmBlendingBeltSize - z_from_start) / constants.csmBlendingBeltSize;
    prev = calculate_csm_shadow_pcf_no_blend(lid, cid - 1, pos, tech, lights, constants, mats);
  }

  if (z_from_end <= constants.csmBlendingBeltSize)
  {
    nextw = (constants.csmBlendingBeltSize - z_from_end) / constants.csmBlendingBeltSize;
    if (cid < CSM_CASCADE_COUNT - 1)
    {
      next = calculate_csm_shadow_pcf_no_blend(lid, cid + 1, pos, tech, lights, constants, mats);
      nextw *= 0.5f;
    }
    else
    {
      next = 1.f;
    }
  }

  if (prevw > 0.f || nextw > 0.f)
  {
    if (prevw > 0.f && nextw > 0.f)
    {
      prevw *= 0.5f;
      nextw *= 0.5f;
    }
    basew = 1.f - prevw - nextw;
  }

  return base * basew + prev * prevw + next * nextw;
}

func calculate_directional_light_data(
  lid: int , pos: float3,
  data: CsmCascadeData, tech: ShadowTechnique,
  lights: UniformLights, constants: Constants,
  mats: StructuredBuffer<LightMatrices>) -> LightData
{
  var ld: LightData;
  ld.intensity = lights.directionalLights[lid].color * lights.directionalLights[lid].intensity;
  ld.direction = -normalize(lights.directionalLights[lid].direction);

  ld.shadow = 1.f;

  if (constants.useDirectionalLightShadows && data.cascade < CSM_CASCADE_COUNT)
  {
    if (shadow_technique_is_pcf(tech))
    {
      ld.shadow = calculate_csm_shadow_pcf(
        lid, data.cascade, pos,
        data.zIntoCascadeStart / data.zCascadeSize,
        data.zIntoCascadeEnd / data.zNextCascadeSize,
        tech, lights, constants, mats);
    }
  }

  return ld;
}

func calculate_point_light_data(
  lid: int, pos: float3, tech: ShadowTechnique,
  lights: UniformLights, constants: Constants,
  mats: StructuredBuffer<LightMatrices>) -> LightData
{
  var ld: LightData;
  ld.direction = normalize(lights.pointLights[lid].position - pos);
  ld.shadow = 1.f;
  let lightIntensity = lights.pointLights[lid].color * lights.pointLights[lid].intensity;
  let lightAttenuation =
    calculate_attenuation(pos, lights.pointLights[lid].position, lights.pointLights[lid].range);
  ld.intensity = lightIntensity * lightAttenuation;
  if (length(ld.intensity) < EPSILON)
    return ld;

  if (constants.usePointLightShadows)
  {
    let sampleDir = -float3(ld.direction.x, ld.direction.y, -ld.direction.z);

    let faceIdx =
      abs(sampleDir.x) > abs(sampleDir.y) && abs(sampleDir.x) > abs(sampleDir.z) ? (sampleDir.x > 0.f ? 0 : 1) :
      abs(sampleDir.y) > abs(sampleDir.z) ? (sampleDir.y > 0.f ? 2 : 3) :
      (sampleDir.z > 0.f ? 4 : 5);

    let posLightClipSpace = mul(float4(pos, 1.f), mats[0].pointLightMats[lid][faceIdx]);
    let posLightSpaceNDC = posLightClipSpace.xyz / posLightClipSpace.w;

    if (shadow_technique_is_pcf(tech))
    {
      ld.shadow = BindlessSamplerCubeShadow(lights.pointLights[lid].shadowmap).SampleCmpLevelZero(sampleDir, posLightSpaceNDC.z);

      if (shadow_technique_is_pcf_kernel(tech))
      {
        let gridDim = PCF_KERNEL_SIZES[tech];
        let mid = gridDim / 2 + 1;

        let faceExt =
          (faceIdx == 0 || faceIdx == 1) ? abs(sampleDir.x) :
          (faceIdx == 2 || faceIdx == 3) ? abs(sampleDir.y) :
          abs(sampleDir.z);

        let baseDir = sampleDir / faceExt;

        // PCF is symmetrical => dir does not matter
        let ud = (2.f / float(POINT_SM_RESOLUTION)) * ((faceIdx == 2 || faceIdx == 3) ? float3(1.f, 0.f, 0.f) : float3(0.f, 1.f, 0.f));
        let vd = (2.f / float(POINT_SM_RESOLUTION)) * ((faceIdx == 4 || faceIdx == 5) ? float3(1.f, 0.f, 0.f) : float3(0.f, 0.f, 1.f));

        var sampleCount: float = 1.f;

        for (var y: int = 0; y < gridDim; ++y)
          for (var x: int = 0; x < gridDim; ++x)
          {
            if (x == mid && y == mid)
              continue;

            let sdir = normalize(baseDir + (float(x) - float(gridDim) * 0.5f) * ud + (float(y) - float(gridDim) * 0.5f) * vd);
            let w = pcf_kernel_weight(x, y, tech);

            ld.shadow += w * BindlessSamplerCubeShadow(lights.pointLights[lid].shadowmap).SampleCmpLevelZero(sdir, posLightSpaceNDC.z);
            sampleCount += w;
          }

        ld.shadow /= sampleCount;
      }
    }
  }

  return ld;
}

func calculate_spot_light_data(
  lid: int , pos: float3, tech: ShadowTechnique,
  lights: UniformLights, constants: Constants,
  mats: StructuredBuffer<LightMatrices>) -> LightData
{
  LightData ld;
  ld.shadow = 1.f;
  let lightIntensity = lights.spotLights[lid].color * lights.spotLights[lid].intensity;
  let lightAttenuation =
    calculate_attenuation(pos, lights.spotLights[lid].position, lights.spotLights[lid].range);

  ld.direction = normalize(lights.spotLights[lid].direction);
  let fromPosDir = normalize(lights.spotLights[lid].position - pos);

  let lightAngularAttenuation = calculate_angular_attenuation(
    dot(ld.direction, -fromPosDir),
    cos(lights.spotLights[lid].innerConeAngle * 0.5f),
    cos(lights.spotLights[lid].outerConeAngle * 0.5f));

  ld.intensity = lightIntensity * lightAttenuation * lightAngularAttenuation;
  if (length(ld.intensity) < EPSILON)
    return ld;

  if (constants.useSpotLightShadows)
  {
    let posLightClipSpace = mul(float4(pos, 1.f), mats[0].spotLightMats[lid]);
    let posLightSpaceNDC = posLightClipSpace.xyz / posLightClipSpace.w;
    let shadowUv = posLightSpaceNDC.xy * 0.5f + 0.5f;

    if (shadow_technique_is_pcf(tech))
    {
      ld.shadow = BindlessSampler2DShadow(lights.spotLights[lid].shadowmap).SampleCmpLevelZero(shadowUv, posLightSpaceNDC.z);

      if (shadow_technique_is_pcf_kernel(tech))
      {
        let gridDim = PCF_KERNEL_SIZES[tech];
        let mid = gridDim / 2 + 1;

        let uvStep = float2(1.f / SPOT_SM_RESOLUTION);
        let uvBase = shadowUv - float(gridDim) * 0.5f * uvStep;

        float sampleCount = 1.f;

        for (int y = 0; y < gridDim; ++y)
          for (int x = 0; x < gridDim; ++x)
          {
            if (x == mid && y == mid)
              continue;

            let uv = uvBase + uvStep * float2(float(x), float(y));
            let w = pcf_kernel_weight(x, y, tech);

            ld.shadow = w * BindlessSampler2DShadow(lights.spotLights[lid].shadowmap).SampleCmpLevelZero(uv, posLightSpaceNDC.z);
            sampleCount += w;
          }

        ld.shadow /= sampleCount;
      }
    }
  }

  return ld;
}

[shader("compute")]
[numthreads(1, 1, 1)]
func transfer_light_matrix(
  lightMats: RWStructuredBuffer<LightMatrices>,
  viewParams: ConstantBuffer<ViewParams>,
  viewData: StructuredBuffer<ViewData>,
  uniform lightId: uint)
{
  let correctedMat = adjust_viewproj_mat(viewParams, viewData[0]);
  if (lightId < POINT_LIGHT_BUF_SIZE * 6)
  {
    let idx = lightId;
    let lidx = idx / 6;
    let fidx = idx - lidx * 6;
    lightMats[0].pointLightMats[lidx][fidx] = correctedMat;
  }
  else if (lightId < POINT_LIGHT_BUF_SIZE * 6 + SPOT_LIGHT_BUF_SIZE)
  {
    let idx = lightId - POINT_LIGHT_BUF_SIZE * 6;
    lightMats[0].spotLightMats[idx] = correctedMat;
  }
  else if (
    lightId <
    POINT_LIGHT_BUF_SIZE * 6 + SPOT_LIGHT_BUF_SIZE + DIRECTIONAL_LIGHT_BUF_SIZE * CSM_CASCADE_COUNT)
  {
    let idx = lightId - POINT_LIGHT_BUF_SIZE * 6 - SPOT_LIGHT_BUF_SIZE;
    let lidx = idx / CSM_CASCADE_COUNT;
    let cidx = idx - lidx * CSM_CASCADE_COUNT;
    lightMats[0].directionalLightMats[lidx][cidx] = correctedMat;
  }
}

