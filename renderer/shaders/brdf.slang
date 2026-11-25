import constants;
import common;

static const float F_DIEL = 0.04f;

func hplus(float v) -> float { return v > 0.f ? 1.f : 0.f; }

func d_ggx(nh: float, alpha2: float) -> float
{
  let nh2 = nh * nh;
  let divterm = nh2 * (alpha2 - 1.f) + 1.f;
  return (alpha2 * float(nh)) / (PI * divterm * divterm);
}

func g_smith(nl: float, nv: float, hl: float, hv: float, alpha2: float) -> float
{
  let sl = sqrt(lerp(nl * nl, 1.f, alpha2));
  let sv = sqrt(lerp(nv * nv, 1.f, alpha2));

  return (4.f * nl * nv * hplus(hl) * hplus(hv)) / ((nl + sl) * (nv + sv));
}

func diffuse_brdf() -> float
{
  return 1.f / PI;
}

func diffuse_btdf() -> float
{
  return 1.f / PI;
}

func specular_brdf(nl: float, nv: float, hl: float, hv: float, nh: float, a2: float) -> float
{
  return (d_ggx(nh, a2) * g_smith(nl, nv, hl, hv, a2)) / max(0.001f, 4.f * nl * nv);
}

func conductor_frensel_shlick(f0: float3, hv: float) -> float3
{
  return lerp(f0, float3(1.f), pow(1.f - abs(hv), 5.f));
}

struct ShadedSurface
{
  var diff: float3;
  var spec: float3;
}

struct ShadingBasicInput
{
  var n: float3;
  var l: float3;
  var v: float3;
}

struct ShadingMaterialProps
{
  var a2:    float;
  var cDiff: float3;
  var f0:    float3;
}

interface IBRDFInput
{
  func extractProps() -> ShadingMaterialProps;   
}

struct ShadingRMInput : IBRDFInput
{
  var metalness: float;
  var roughness: float;
  var albedo:    float3;

  func extractProps() -> ShadingMaterialProps
  {
    var output: ShadingMaterialProps = {};
    let c = albedo;
    let a = roughness * roughness;
    output.a2 = a * a;
    output.cDiff = lerp(c, float3(0.0f), metalness);
    output.f0 = lerp(float3(F_DIEL), c, metalness);
    return output;
  }
}

struct ShadingDSGInput : IBRDFInput
{
  var diffuse:    float3;
  var specular:   float3;
  var glossiness: float;

  func extractProps() -> ShadingMaterialProps
  {
    var output: ShadingMaterialProps = {};
    let roughness = 1.f - glossiness;
    let a = roughness * roughness;
    output.a2 = a * a;
    output.cDiff = diffuse * (1.f - max(specular.x, max(specular.y, specular.z)));
    output.f0 = specular;
    return output;
  }
}

struct ShadingFoliageInput
{
  var transmission: float;
  var transCol:     float3;
}

func pbr_shade<TBRDFInput: IBRDFInput>(
  base: ShadingBasicInput,
  model: TBRDFInput,
  foliage: ShadingFoliageInput) -> ShadedSurface
{
  let nn = normalize(base.n);
  let ll = normalize(base.l);
  let vv = normalize(base.v);
  let hh = normalize(ll + vv);
  let nv = max(dot(nn, vv), 0.f);
  let hl = max(dot(hh, ll), 0.f);
  let hv = max(dot(hh, vv), 0.f);
  let nh = max(dot(nn, hh), 0.f);

  let nlu = dot(nn, ll);
  let nl = max(nlu, 0.f);

  let isTrasnmissive = foliage.transmission > EPSILON;

  if (nv < EPSILON)
    return ShadedSurface(float3(0.f), float3(0.f));
  if (!isTrasnmissive && nl < EPSILON)
    return ShadedSurface(float3(0.f), float3(0.f));

  let matProps = model.extractProps();
  let f = conductor_frensel_shlick(matProps.f0, hv);

  let specBsdf = float3(nl * specular_brdf(nl, nv, hl, hv, nh, matProps.a2));
  var diffBsdf = float3(nl * diffuse_brdf());

  if (isTrasnmissive)
  {
    let inlu = -dot(nn, ll);
    let wrapFactor = lerp(0.f, 0.5f, luminance_bt601(foliage.transCol));
    let wrapNormalizationFactor = 1.f / ((1.f + wrapFactor) * (1.f + wrapFactor));
    let frontFactor = max((nlu + wrapFactor) * wrapNormalizationFactor, 0.f);
    let backFactor = max((inlu + wrapFactor) * wrapNormalizationFactor, 0.f);
    diffBsdf = lerp(float3(frontFactor * diffuse_brdf()), backFactor * diffuse_btdf() * foliage.transCol, foliage.transmission);
  }

  return ShadedSurface(
    (1.f - f) * diffBsdf * matProps.cDiff,
    f * specBsdf);
}
