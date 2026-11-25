import constants;
import common;

func get_envi_ambient_from_skybox(sb: SkyboxSourceData, constants: Constants) -> float3
{
  var ambientCol = float3(1.f);
  if (constants.useSkybox && constants.useSkyboxForAmbient)
  {
    let cubemap = BindlessSamplerCube<float3>(sb.cubemapTexSmp);
    let maxLod = floor(log2(cubemap.getSize().x));
    ambientCol *= cubemap.SampleLevel(float3(0.f, 1.f, 0.f), maxLod);
  }
  return ambientCol;
}

func sample_skybox(dir: float3, sb: SkyboxSourceData, constants: Constants) -> float3
{
  if (constants.useSkybox)
    return BindlessSamplerCube<float3>(sb.cubemapTexSmp).Sample(dir);
  else
    return float3(0.f);
}

func sample_sky_light(
  dir: float3, sb: SkyboxSourceData, lights: UniformLights, constants: Constants) -> float3
{
  let skyColor = sample_skybox(dir, sb, constants);
  var sunColor = float3(0.f);
  if (lights.directionalLightsCount > 0)
  {
    let dirToSun = -normalize(lights.directionalLights[0].direction);
    let sunCol = lights.directionalLights[0].intensity * lights.directionalLights[0].color;
    let cosToSky = clamp(dot(dir, dirToSun), 0.f, 1.f);
    sunColor = sunCol * pow(cosToSky, 3500.f);
  }
  return skyColor + sunColor;
}
