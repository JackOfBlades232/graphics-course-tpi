#ifndef BINDLESS_H_INCLUDED
#define BINDLESS_H_INCLUDED

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
  return texture(
    sampler2D(
      bindlessTextures[nonuniformEXT(texSmpIds.x)],
      bindlessSamplers[nonuniformEXT(texSmpIds.y)]),
    uv);
}

vec4 sample_bindless_tex_lod(TexSmpIdPair ids, vec2 uv, float lod)
{
  if (ids == NO_TEXTURE_ID)
    return vec4(1.0f);

  const uvec2 texSmpIds = unpack_tex_smp_id_pair(ids);
  return textureLod(
    sampler2D(
      bindlessTextures[nonuniformEXT(texSmpIds.x)],
      bindlessSamplers[nonuniformEXT(texSmpIds.y)]),
    uv, lod);
}

#endif // BINDLESS_H_INCLUDED
