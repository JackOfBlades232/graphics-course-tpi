import constants;
import common;

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

// @TODO: shadowing once we have bindless

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

struct LightData
{
  var intensity: float3;
  var direction: float3;
  var shadow: float;
}

func calculate_directional_light_data(
  lid: int , pos: float3,
  data: CsmCascadeData, tech: ShadowTechnique,
  lights: UniformLights) -> LightData
{
  var ld: LightData;
  ld.intensity = lights.directionalLights[lid].color * lights.directionalLights[lid].intensity;
  ld.direction = -normalize(lights.directionalLights[lid].direction);

  ld.shadow = 1.f; // @TODO: impl

  return ld;
}

func calculate_point_light_data(
  lid: int , pos: float3,
  tech: ShadowTechnique, lights: UniformLights) -> LightData
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
  // @TODO: impl shadow
  return ld;
}

func calculate_spot_light_data(
  lid: int , pos: float3,
  tech: ShadowTechnique, lights: UniformLights) -> LightData
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

  // @TODO: impl shadow

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

