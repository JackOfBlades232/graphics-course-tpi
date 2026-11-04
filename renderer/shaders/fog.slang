import constants;
import common;
import lighting;

func sample_perlin(tex: Sampler3D<float>, p: float3) -> float
{
  return tex.SampleLevel(p / 32.f, 0) * 2.f - 1.f;
}

func fbm(pos: float3, octaves: int) -> float
{
  var p = pos;
  var value: float = 0.f;
  var amplitude: float = 0.5f;

  for (var i: int = 0; i < octaves; i++)
  {
    value += amplitude * perlin(p);

    p *= 2.f;
    amplitude *= 0.5f;
  }

  return 0.5f * value + 0.5f;
}

func density_noise(
  p: float3, rng_min: float, rng_max: float,
  n: int, scale: float, wind_inf: float,
  constants: Constants) -> float
{
  let wind2d = -constants.weatherData.windDirection * constants.weatherData.windStrength;
  let wind = float3(wind2d.x, 0.f, wind2d.y) * constants.time;
  let base = fbm((p + wind * wind_inf) / scale, n);
  return smoothstep(rng_min, rng_max, base);
}

func density_base(p: float3, constants: Constants) -> float
{
  let height = exp(-max(p.y - constants.weatherData.fogHBase, 0.f) * constants.weatherData.fogHC);
  return height * constants.weatherData.fogRho0;
}

func density(p: float3, dist: float, constants: Constants) -> float
{
  let wind2d = -constants.weatherData.windDirection * constants.weatherData.windStrength;
  let wind = float3(wind2d.x, 0.f, wind2d.y) * constants.time;
  var shapeFaded: float = 1.f;
  if (constants.weatherData.fogShapeScale > EPSILON)
  {
    let shapeFadeout = smoothstep(
      0.f, 1.f,
      (dist - constants.weatherData.fogShapeFadeoutStart) / constants.weatherData.fogShapeFadeoutSize);
    var shape: float = 1.f;
    if (shapeFadeout < 0.9999f)
    {
      shape = density_noise(
        p,
        constants.weatherData.fogShapeRngMin,
        constants.weatherData.fogShapeRngMax,
        3,
        constants.weatherData.fogShapeScale,
        constants.weatherData.fogShapeWindInfluence,
        constants);
    }
    shapeFaded = lerp(shape, 1.f, shapeFadeout);
  }
  return density_base(p, constants) * shapeFaded;
}

[shader("compute")]
[numthreads(16, 16, 1)]
func fog_generate(
  threadId: uint3 : SV_DispatchThreadID,
  fogHalfres: RWTexture2D<float4>,
  gbufDepth: Sampler2D<float>,
  perlinNoise: Sampler3D<float>,
  source: ConstantBuffer<FogSourceData>,
  constants: ConstantBuffer<Constants>,
  viewParams: ConstantBuffer<ViewParams>,
  lights: ConstantBuffer<UniformLights>)
{
  let imCoord = threadId.xy;
  let imSize = uint2(constants.mainTargetResolution) / 2;

  if (imCoord.x >= imSize.x || imCoord.y >= imSize.y)
    return;

  let scatterAllLights = constants.fogSceneLightsScatterCoeff >= EPSILON;

  let invaspect = float2(1.f / viewParams.mProj[0][0], 1.f / viewParams.mProj[1][1]);
  let uv = float2(2 * imCoord + 1) * constants.mainTargetInverseResolution;
  let ndcuv = 2.f * uv - 1.f;
  let ray = float3(ndcuv * invaspect, 1.f);
  let raylen = length(ray);
  let dir = ray / raylen;

  let clipDepth = gbufDepth.SampleLevel(uv, 0).x;
  let linearDepth = linearize_rev_z(clipDepth, viewParams.viewFrustum.nearZ, viewParams.viewFrustum.farZ);
  let worldDepth = raylen * linearDepth;

  let worldDir = mul(dir, float3x3(viewParams.mInverseView));

  var pos: float3 = viewParams.viewPos;
  var vpos = float3(0.f);
  var transmitted = float4(0.f, 0.f, 0.f, 1.f);
  var dist: float = 0.f;
  var pdd: float = density(pos, dist, constants);

  let stepSize = min(worldDepth / constants.fogRaymarchSteps, constants.fogMaxStepSize);
  let posStep = worldDir * stepSize;
  let vposStep = dir * stepSize;

  for (var s: int = 0; s < constants.fogRaymarchSteps; ++s)
  {
    let ndd = density(pos, dist, constants);
    let dd = (ndd + pdd) * 0.5f;
    pdd = ndd;
    if (dd < 0.000001f)
    {
      pos += posStep;
      vpos += vposStep;
      dist += stepSize;
      continue;
    }

    var ii: float = constants.weatherData.fogInscatterC;

    let stp = exp(-dd * stepSize); 
    transmitted.a *= stp;

    if (ii > 0.f)
    {
      // @TODO: lighting

      let csmd = get_cascade_data_for_view_pos(vpos, viewParams);

      for (var i: int = 0; i < (scatterAllLights ? lights.directionalLightsCount : min(1, lights.directionalLightsCount)); ++i)
      {
        let ld = calculate_directional_light_data(i, pos, csmd, ShadowTechnique.SIMPLE, lights);
        let inscatter = ii * ld.intensity * ld.shadow; // * phase
        transmitted.rgb += transmitted.a * inscatter * stp;
      }

      for (int i = 0; i < (scatterAllLights ? lights.pointLightsCount : 0); ++i)
      {
        let ld = calculate_point_light_data(i, pos, ShadowTechnique.SIMPLE, lights);
        let inscatter = ii * ld.intensity * ld.shadow * constants.fogSceneLightsScatterCoeff; // * phase
        transmitted.rgb += transmitted.a * inscatter * stp;
      }
      for (int i = 0; i < (scatterAllLights ? lights.spotLightsCount : 0); ++i)
      {
        let ld = calculate_spot_light_data(i, pos, ShadowTechnique.SIMPLE, lights);
        let inscatter = ii * ld.intensity * ld.shadow * constants.fogSceneLightsScatterCoeff; // * phase
        transmitted.rgb += transmitted.a * inscatter * stp;
      }
    }

    pos += posStep;
    vpos += vposStep;
    dist += stepSize;
  }

  let finalTracedLinearDepth = vpos.z;
  let finDepth = min(linearDepth, viewParams.viewFrustum.farZ);
  if (finDepth > finalTracedLinearDepth)
  {
    let startPoint = pos;
    let endPoint = viewParams.viewPos + (pos - viewParams.viewPos) * (finDepth / finalTracedLinearDepth);
    let distantStepSize = raylen * (finDepth - finalTracedLinearDepth) / constants.fogDistantSteps;
    let distantStep = worldDir * distantStepSize;
    for (var s: int = 0; s < constants.fogDistantSteps; ++s)
    {
      transmitted.a *= exp(-density_base(pos, constants) * distantStepSize);
      pos += distantStep;
    }
  }

  fogHalfres[imCoord] = transmitted;
}

func upscale_sample_fog(tex: Sampler2D<float4>, uv: float2) -> float4
{
  return sample_tex_catmull_rom_5tap(tex, uv, tex.getSize(), 0.f);
}

[shader("compute")]
[numthreads(16, 16, 1)]
func fog_apply(
  threadId: uint3 : SV_DispatchThreadID,
  hdrTarget: RWTexture2D<float4>,
  fogHalfres: Sampler2D<float4>,
  source: ConstantBuffer<FogSourceData>,
  constants: ConstantBuffer<Constants>)
{
  let imCoord = threadId.xy;
  let imSize = uint2(constants.mainTargetResolution);

  if (imCoord.x >= imSize.x || imCoord.y >= imSize.y)
    return;

  let baseColor = hdrTarget[imCoord].xyz;
  let uv = float2(imCoord) * constants.mainTargetInverseResolution;
  let fog = upscale_sample_fog(fogHalfres, uv);

  let finalColor = fog.rgb + lerp(source.color, baseColor, clamp(fog.a, 0.f, 1.f));
  hdrTarget[imCoord] = float4(finalColor, 1.f);
}

static const int PERM[64] = int[64](
  17, 5, 29, 12, 8, 23, 1, 30,
  15, 26, 10, 19, 3, 21, 14, 28,
  7, 31, 11, 24, 2, 18, 27, 6,
  13, 0, 25, 20, 9, 22, 4, 16,

  17, 5, 29, 12, 8, 23, 1, 30,
  15, 26, 10, 19, 3, 21, 14, 28,
  7, 31, 11, 24, 2, 18, 27, 6,
  13, 0, 25, 20, 9, 22, 4, 16);

func fade(t: float) -> float
{
  return t * t * t * (t * (t * 6.f - 15.f) + 10.f);
}

func grad(hash: int, p: float3) -> float
{
  let h = hash & 15;

  let u = h < 8 ? p.x : p.y;

  var v: float;
  if (h < 4)
    v = p.y;
  else if (h == 12 || h == 14)
    v = p.x;
  else
    v = p.z;

  return ((h & 1) == 0 ? u : -u) + ((h & 2) == 0 ? v : -v);
}

func perlin(p: float3) -> float
{
  let i = int3(floor(p));
  let f = fract(p);

  let u = float3(fade(f.x), fade(f.y), fade(f.z));

  let X = i.x & 31;
  let Y = i.y & 31;
  let Z = i.z & 31;

  let A  = PERM[X] + Y;
  let AA = PERM[A] + Z;
  let AB = PERM[A + 1] + Z;

  let B  = PERM[X + 1] + Y;
  let BA = PERM[B] + Z;
  let BB = PERM[B + 1] + Z;

  let x1 = lerp(grad(PERM[AA], f), grad(PERM[BA], f - float3(1.f, 0.f, 0.f)), u.x);
  let x2 = lerp(grad(PERM[AB], f - float3(0.f, 1.f, 0.f)), grad(PERM[BB], f - float3(1.f, 1.f, 0.f)), u.x);
  let y1 = lerp(x1, x2, u.y);
  let x3 = lerp(grad(PERM[AA + 1], f - float3(0.f, 0.f, 1.f)), grad(PERM[BA + 1], f - float3(1.f, 0.f, 1.f)), u.x);
  let x4 = lerp(grad(PERM[AB + 1], f - float3(0.f, 1.f, 1.f)), grad(PERM[BB + 1], f - float3(1.f, 1.f, 1.f)), u.x);

  let y2 = lerp(x3, x4, u.y);

  return lerp(y1, y2, u.z);
}

[shader("compute")]
[numthreads(8, 8, 8)]
func fog_noise_pregen(
  threadId: uint3 : SV_DispatchThreadID,
  perlinNoise: RWTexture3D<float>)
{
  let imCoord = threadId;
  let imSize = perlinNoise.getSize();

  if (imCoord.x >= imSize.x || imCoord.y >= imSize.y || imCoord.z >= imSize.z)
    return;

  let noiseCoord = float3(imCoord) * 32.f / float3(imSize);
  perlinNoise[imCoord] = perlin(noiseCoord) * 0.5f + 0.5f;
}
