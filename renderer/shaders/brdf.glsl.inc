#ifndef BRDF_H_INCLUDED
#define BRDF_H_INCLUDED

#include "cpp_glsl_compat.h"


// @TODO dedup with tonemap
float luminance_bt601(vec3 col)
{
  return 0.299f * col.r + 0.587f * col.g + 0.114f * col.b;
}

// @TODO add (back) anisotropy?
// @TODO refactor these functions into proper brdf calculations without dups

const float F_DIEL = 0.04f;

#define HPLUS(v_) ((v_) > 0.f ? 1.f : 0.f)

float d_ggx(float nh, float alpha2)
{
  float nh2 = nh * nh;
  float divterm = nh2 * (alpha2 - 1.f) + 1.f;
  return (alpha2 * HPLUS(nh)) / (SHADER_PI * divterm * divterm);
}

float g_smith(float nl, float nv, float hl, float hv, float alpha2)
{
  float sl = sqrt(mix(nl * nl, 1.f, alpha2));
  float sv = sqrt(mix(nv * nv, 1.f, alpha2));

  return (4.f * nl * nv * HPLUS(hl) * HPLUS(hv)) / ((nl + sl) * (nv + sv));
}

float diffuse_brdf()
{
  return 1.f / SHADER_PI;
}

float diffuse_btdf()
{
  return 1.f / SHADER_PI;
}

float specular_brdf(float nl, float nv, float hl, float hv, float nh, float a2)
{
  return (d_ggx(nh, a2) * g_smith(nl, nv, hl, hv, a2)) / max(0.001f, 4.f * nl * nv);
}

vec3 conductor_frensel_shlick(vec3 f0, float hv)
{
  return mix(f0, vec3(1.f), pow(1.f - abs(hv), 5.f));
}

void shade_cook_torrance(
  vec3 n, vec3 l, vec3 v, float metalness, float roughness, vec3 albedo, float transmission, vec3 transCol,
  out vec3 diff, out vec3 spec)
{
  vec3 c = albedo;

  vec3 c_diff = mix(c, vec3(0.0f), metalness);

  vec3 nn = normalize(n);
  vec3 ll = normalize(l);
  vec3 vv = normalize(v);
  vec3 hh = normalize(ll + vv);
  float nv = max(dot(nn, vv), 0.f);
  float hl = max(dot(hh, ll), 0.f);
  float hv = max(dot(hh, vv), 0.f);
  float nh = max(dot(nn, hh), 0.f);

  float nlu = dot(nn, ll);
  float nl = max(nlu, 0.f);

  if (nv < SHADER_EPSILON)
  {
    diff = spec = vec3(0.f);
    return;
  }

  bool isTrasnmissive = transmission > SHADER_EPSILON; 

  if (!isTrasnmissive && nl < SHADER_EPSILON)
  {
    diff = spec = vec3(0.f);
    return;
  }

  float inlu = -dot(nn, ll);

  float a = roughness * roughness;
  float a2 = a * a;

  vec3 f0 = mix(vec3(F_DIEL), c, metalness);
  vec3 f = conductor_frensel_shlick(f0, hv);

  vec3 spec_bsdf = vec3(nl * specular_brdf(nl, nv, hl, hv, nh, a2));
  vec3 diff_bsdf;
  if (isTrasnmissive)
  {
    float wrap = mix(0.f, 0.5f, luminance_bt601(transCol));
    float wrapNormalizationFactor = 1.f / ((1.f + wrap) * (1.f + wrap));
    float frontFactor = max((nlu + wrap) * wrapNormalizationFactor, 0.f);
    float backFactor = max((inlu + wrap) * wrapNormalizationFactor, 0.f);
    diff_bsdf = mix(vec3(frontFactor * diffuse_brdf()), backFactor * diffuse_btdf() * transCol, transmission);
  }
  else
  {
    diff_bsdf = vec3(nl * diffuse_brdf());
  }

  diff = (1.f - f) * diff_bsdf * c_diff;
  spec = f * spec_bsdf;
}

void shade_cook_torrance_diffuse_spec_gloss(
  vec3 n, vec3 l, vec3 v, vec3 diffuse, vec3 specular, float glossiness, float transmission, vec3 transCol,
  out vec3 diff, out vec3 spec)
{
  vec3 nn = normalize(n);
  vec3 ll = normalize(l);
  vec3 vv = normalize(v);
  vec3 hh = normalize(ll + vv);
  float nv = max(dot(nn, vv), 0.f);
  float hl = max(dot(hh, ll), 0.f);
  float hv = max(dot(hh, vv), 0.f);
  float nh = max(dot(nn, hh), 0.f);

  float nlu = dot(nn, ll);
  float nl = max(nlu, 0.f);

  if (nv < SHADER_EPSILON)
  {
    diff = spec = vec3(0.f);
    return;
  }

  bool isTrasnmissive = transmission > SHADER_EPSILON; 

  if (!isTrasnmissive && nl < SHADER_EPSILON)
  {
    diff = spec = vec3(0.f);
    return;
  }

  float inlu = -dot(nn, ll);

  float roughness = 1.f - glossiness;
  float a = roughness * roughness;
  float a2 = a * a;

  vec3 f0 = specular;

  vec3 c_diff = diffuse * (1.f - max(specular.r, max(specular.g, specular.b)));

  vec3 f = conductor_frensel_shlick(f0, hv);

  vec3 spec_bsdf = vec3(nl * specular_brdf(nl, nv, hl, hv, nh, a2));
  vec3 diff_bsdf;
  if (isTrasnmissive)
  {
    float wrap = mix(0.f, 0.5f, luminance_bt601(transCol));
    float wrapNormalizationFactor = 1.f / ((1.f + wrap) * (1.f + wrap));
    float frontFactor = max((nlu + wrap) * wrapNormalizationFactor, 0.f);
    float backFactor = max((inlu + wrap) * wrapNormalizationFactor, 0.f);
    diff_bsdf = mix(vec3(frontFactor * diffuse_brdf()), backFactor * diffuse_btdf() * transCol, transmission);
  }
  else
  {
    diff_bsdf = vec3(nl * diffuse_brdf());
  }

  diff = (1.f - f) * diff_bsdf * c_diff;
  spec = f * spec_bsdf;
}

void calculate_pbr(
  vec3 normal, vec3 lightDir, vec3 viewVec,
  float metalness, float roughness, vec3 albedo, float transmission, vec3 transCol,
  out vec3 diff, out vec3 spec)
{
  shade_cook_torrance(
    normal, lightDir, viewVec, metalness, roughness, albedo, transmission, transCol,
    diff, spec);
}

void calculate_pbr_diff_spec_gloss(
  vec3 normal, vec3 lightDir, vec3 viewVec,
  vec3 diffuse, vec3 specular, float glossiness, float transmission, vec3 transCol,
  out vec3 diff, out vec3 spec)
{
  shade_cook_torrance_diffuse_spec_gloss(
    normal, lightDir, viewVec, diffuse, specular, glossiness, transmission, transCol,
    diff, spec);
}

#endif // BRDF_H_INCLUDED

