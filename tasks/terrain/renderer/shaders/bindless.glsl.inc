#ifndef BINDLESS_H_INCLUDED
#define BINDLESS_H_INCLUDED

#include "common.frag.inc"

#define BINDLESS_SAMPLER(ids_)                 \
  sampler2D(                                   \
    bindlessTextures[nonuniformEXT((ids_).x)], \
    bindlessSamplers[nonuniformEXT((ids_).y)])

layout(binding = 0, set = 1) readonly buffer material_params_t
{
  Material materialParams[];
};
layout(binding = 0, set = 2) uniform texture2D bindlessTextures[];
layout(binding = 0, set = 3) uniform sampler bindlessSamplers[];

vec4 sample_bindless_tex(TexSmpIdPair ids, vec2 uv)
{
  if (ids == NO_TEXTURE_ID)
    return vec4(1.0f);

  const uvec2 texSmpIds = unpack_tex_smp_id_pair(ids);
  return texture(BINDLESS_SAMPLER(texSmpIds), uv);
}

vec4 sample_bindless_tex_lod(TexSmpIdPair ids, vec2 uv, float lod)
{
  if (ids == NO_TEXTURE_ID)
    return vec4(1.0f);

  const uvec2 texSmpIds = unpack_tex_smp_id_pair(ids);
  return textureLod(BINDLESS_SAMPLER(texSmpIds), uv, lod);
}

vec4 sample_bindless_tex_bicubic(TexSmpIdPair ids, vec2 uv)
{
  if (ids == NO_TEXTURE_ID)
    return vec4(1.0f);

  const uvec2 texSmpIds = unpack_tex_smp_id_pair(ids);

  const vec2 size = textureSize(BINDLESS_SAMPLER(texSmpIds), 0);
  const vec2 invSize = 1.f / size;

  vec2 puv = uv * size - 0.5f;
  const vec2 fpuv = fract(puv);
  puv += 0.5f - fpuv;

  const vec4 hcoeff = catmull_rom_cubic_coeffs(fpuv.x);
  const vec4 vcoeff = catmull_rom_cubic_coeffs(fpuv.y);

  const vec2 hstep = vec2(invSize.x, 0.f);
  const vec2 vstep = vec2(0.f, invSize.y);

  const vec2 upuv = puv * invSize;

  vec4 p0, p1, p2, p3;

  {
    const vec2 base = upuv - vstep;

    const vec4 p00 = texture(BINDLESS_SAMPLER(texSmpIds), base - hstep);
    const vec4 p01 = texture(BINDLESS_SAMPLER(texSmpIds), base);
    const vec4 p02 = texture(BINDLESS_SAMPLER(texSmpIds), base + hstep);
    const vec4 p03 = texture(BINDLESS_SAMPLER(texSmpIds), base + 2.f * hstep);

    p0 = p00 * hcoeff.x + p01 * hcoeff.y + p02 * hcoeff.z + p03 * hcoeff.w;
  }
  {
    const vec2 base = upuv;

    const vec4 p10 = texture(BINDLESS_SAMPLER(texSmpIds), base - hstep);
    const vec4 p11 = texture(BINDLESS_SAMPLER(texSmpIds), base);
    const vec4 p12 = texture(BINDLESS_SAMPLER(texSmpIds), base + hstep);
    const vec4 p13 = texture(BINDLESS_SAMPLER(texSmpIds), base + 2.f * hstep);

    p1 = p10 * hcoeff.x + p11 * hcoeff.y + p12 * hcoeff.z + p13 * hcoeff.w;
  }
  {
    const vec2 base = upuv + vstep;

    const vec4 p20 = texture(BINDLESS_SAMPLER(texSmpIds), base - hstep);
    const vec4 p21 = texture(BINDLESS_SAMPLER(texSmpIds), base);
    const vec4 p22 = texture(BINDLESS_SAMPLER(texSmpIds), base + hstep);
    const vec4 p23 = texture(BINDLESS_SAMPLER(texSmpIds), base + 2.f * hstep);

    p2 = p20 * hcoeff.x + p21 * hcoeff.y + p22 * hcoeff.z + p23 * hcoeff.w;
  }
  {
    const vec2 base = upuv + 2.f * vstep;

    const vec4 p30 = texture(BINDLESS_SAMPLER(texSmpIds), base - hstep);
    const vec4 p31 = texture(BINDLESS_SAMPLER(texSmpIds), base);
    const vec4 p32 = texture(BINDLESS_SAMPLER(texSmpIds), base + hstep);
    const vec4 p33 = texture(BINDLESS_SAMPLER(texSmpIds), base + 2.f * hstep);

    p3 = p30 * hcoeff.x + p31 * hcoeff.y + p32 * hcoeff.z + p33 * hcoeff.w;
  }

  return p0 * vcoeff.x + p1 * vcoeff.y + p2 * vcoeff.z + p3 * vcoeff.w;
}

vec4 sample_bindless_tex_bicubic_lod(TexSmpIdPair ids, vec2 uv, float lod)
{
  if (ids == NO_TEXTURE_ID)
    return vec4(1.0f);

  const uvec2 texSmpIds = unpack_tex_smp_id_pair(ids);

  // @TODO: lod here?
  const vec2 size = textureSize(BINDLESS_SAMPLER(texSmpIds), 0);
  const vec2 invSize = 1.f / size;

  vec2 puv = uv * size - 0.5f;
  const vec2 fpuv = fract(puv);
  puv += 0.5f - fpuv;

  const vec4 hcoeff = catmull_rom_cubic_coeffs(fpuv.x);
  const vec4 vcoeff = catmull_rom_cubic_coeffs(fpuv.y);

  const vec2 hstep = vec2(invSize.x, 0.f);
  const vec2 vstep = vec2(0.f, invSize.y);

  const vec2 upuv = puv * invSize;

  vec4 p0, p1, p2, p3;

  {
    const vec2 base = upuv - vstep;

    const vec4 p00 = textureLod(BINDLESS_SAMPLER(texSmpIds), base - hstep, lod);
    const vec4 p01 = textureLod(BINDLESS_SAMPLER(texSmpIds), base, lod);
    const vec4 p02 = textureLod(BINDLESS_SAMPLER(texSmpIds), base + hstep, lod);
    const vec4 p03 = textureLod(BINDLESS_SAMPLER(texSmpIds), base + 2.f * hstep, lod);

    p0 = p00 * hcoeff.x + p01 * hcoeff.y + p02 * hcoeff.z + p03 * hcoeff.w;
  }
  {
    const vec2 base = upuv;

    const vec4 p10 = textureLod(BINDLESS_SAMPLER(texSmpIds), base - hstep, lod);
    const vec4 p11 = textureLod(BINDLESS_SAMPLER(texSmpIds), base, lod);
    const vec4 p12 = textureLod(BINDLESS_SAMPLER(texSmpIds), base + hstep, lod);
    const vec4 p13 = textureLod(BINDLESS_SAMPLER(texSmpIds), base + 2.f * hstep, lod);

    p1 = p10 * hcoeff.x + p11 * hcoeff.y + p12 * hcoeff.z + p13 * hcoeff.w;
  }
  {
    const vec2 base = upuv + vstep;

    const vec4 p20 = textureLod(BINDLESS_SAMPLER(texSmpIds), base - hstep, lod);
    const vec4 p21 = textureLod(BINDLESS_SAMPLER(texSmpIds), base, lod);
    const vec4 p22 = textureLod(BINDLESS_SAMPLER(texSmpIds), base + hstep, lod);
    const vec4 p23 = textureLod(BINDLESS_SAMPLER(texSmpIds), base + 2.f * hstep, lod);

    p2 = p20 * hcoeff.x + p21 * hcoeff.y + p22 * hcoeff.z + p23 * hcoeff.w;
  }
  {
    const vec2 base = upuv + 2.f * vstep;

    const vec4 p30 = textureLod(BINDLESS_SAMPLER(texSmpIds), base - hstep, lod);
    const vec4 p31 = textureLod(BINDLESS_SAMPLER(texSmpIds), base, lod);
    const vec4 p32 = textureLod(BINDLESS_SAMPLER(texSmpIds), base + hstep, lod);
    const vec4 p33 = textureLod(BINDLESS_SAMPLER(texSmpIds), base + 2.f * hstep, lod);

    p3 = p30 * hcoeff.x + p31 * hcoeff.y + p32 * hcoeff.z + p33 * hcoeff.w;
  }

  return p0 * vcoeff.x + p1 * vcoeff.y + p2 * vcoeff.z + p3 * vcoeff.w;
}

#endif // BINDLESS_H_INCLUDED
