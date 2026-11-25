import postfx_vs;
import constants;
import common;
import brdf;
import lighting;
import skybox;

struct LitOutput
{
  var color: float4 : SV_Target;
}

[shader("fragment")]
func resolve(
  texCoord: float2 : TEXCOORD,
  gbufAlbedo: Sampler2D<float3>,
  gbufMaterial: Sampler2D<float4>,
  gbufNormal: Sampler2D<float3>,
  gbufTransmission: Sampler2D<float4>,
  gbufDepth: Sampler2D<float>,
  aoBuf: Sampler2D<float>,
  constants: ConstantBuffer<Constants>,
  lights: ConstantBuffer<UniformLights>,
  lightMats: StructuredBuffer<LightMatrices>,
  viewParams: ConstantBuffer<ViewParams>,
  skybox: ConstantBuffer<SkyboxSourceData>) -> LitOutput
{
  // Unpack gbuffer
  let depth = gbufDepth.SampleLevel(texCoord, 0);

  let pos = depth_and_tc_to_pos(depth, texCoord, viewParams.mInverseProjView);
  let viewVec = normalize(viewParams.viewPos - pos);
  let viewPos = mul(float4(pos, 1.f), viewParams.mView).xyz;

  if (depth <= 0.f)
  {
    // @TODO: unjitter skybox UV
    return LitOutput(
      float4(sample_sky_light(normalize(pos - viewParams.viewPos), skybox, lights, constants), 1.f));
  }

  let albedo = gbufAlbedo.SampleLevel(texCoord, 0);
  let matData = gbufMaterial.SampleLevel(texCoord, 0);
  let normal = gbufNormal.SampleLevel(texCoord, 0);
  let transData = gbufTransmission.SampleLevel(texCoord, 0);

  var ao: float = 1.f;
  if (constants.useSsao)
    ao = aoBuf.SampleLevel(texCoord, 0);

  let mat = uint(matData.x + 0.001f);
  
  // Calculate lighting
  
  let ambient = ao * albedo * constants.ambientLightCoeff * get_envi_ambient_from_skybox(skybox, constants);

  // For directional shadows
  let csmd = get_cascade_data_for_view_pos(viewPos, viewParams);

  let debugMultiplier = get_csm_cascade_debug_multiplier(csmd, constants);

  if (mat != MaterialType.PBR && mat != MaterialType.DIFFUSE)
    return LitOutput(debugMultiplier * float4(1.f, 0.f, 1.f, 1.f));

  var totSurface: ShadedSurface = {0.f, 0.f};
  let foliage: ShadingFoliageInput = {transData.w, transData.xyz};

  var rm: ShadingRMInput = {matData.y, matData.z, albedo};
  var dsg: ShadingDSGInput = {albedo, dequantize4fcol(asint(matData.y)).xyz, matData.z};

  for (var i: int = 0; i < lights.directionalLightsCount; ++i)
  {
    let ld = calculate_directional_light_data(
      i, pos, csmd, constants.directionalLightShadowsTechnique, lights, constants, lightMats);
    if (length(ld.intensity) < EPSILON)
      continue;

    let geom: ShadingBasicInput = {normal, ld.direction, viewVec};
    var surface: ShadedSurface = {};

    if (mat == MaterialType.PBR)
      surface = pbr_shade(geom, rm, foliage);
    else if (mat == MaterialType.DIFFUSE)
      surface = pbr_shade(geom, dsg, foliage);

    totSurface.diff += surface.diff * ld.shadow * ld.intensity;
    totSurface.spec += surface.spec * ld.shadow * ld.intensity;
  }

  for (var i: int = 0; i < lights.pointLightsCount; ++i)
  {
    let ld = calculate_point_light_data(
      i, pos, constants.pointLightShadowsTechnique, lights, constants, lightMats);
    if (length(ld.intensity) < EPSILON)
      continue;

    let geom: ShadingBasicInput = {normal, ld.direction, viewVec};
    var surface: ShadedSurface = {};

    if (mat == MaterialType.PBR)
      surface = pbr_shade(geom, rm, foliage);
    else if (mat == MaterialType.DIFFUSE)
      surface = pbr_shade(geom, dsg, foliage);

    totSurface.diff += surface.diff * ld.shadow * ld.intensity;
    totSurface.spec += surface.spec * ld.shadow * ld.intensity;
  }

  for (var i: int = 0; i < lights.spotLightsCount; ++i)
  {
    let ld = calculate_spot_light_data(
      i, pos, constants.spotLightShadowsTechnique, lights, constants, lightMats);
    if (length(ld.intensity) < EPSILON)
      continue;

    let geom: ShadingBasicInput = {normal, ld.direction, viewVec};
    var surface: ShadedSurface = {};

    if (mat == MaterialType.PBR)
      surface = pbr_shade(geom, rm, foliage);
    else if (mat == MaterialType.DIFFUSE)
      surface = pbr_shade(geom, dsg, foliage);

    totSurface.diff += surface.diff * ld.shadow * ld.intensity;
    totSurface.spec += surface.spec * ld.shadow * ld.intensity;
  }

  let color = ambient + totSurface.diff + totSurface.spec;
  return LitOutput(debugMultiplier * float4(color, 1.f));
}
