#version 450
#extension GL_ARB_separate_shader_objects : enable

layout(location = 0) out vec4 color;

layout(binding = 0) uniform sampler2D colorTex;

layout(push_constant) uniform params_t
{
  vec2 colorRange;
  uint mask;
  uint pad1_;
} params;

layout(location = 0 ) in VS_OUT
{
  vec2 texCoord;
} surf;

void main()
{
  const vec4 base =
    (textureLod(colorTex, surf.texCoord, 0) - params.colorRange.x) /
    (params.colorRange.y - params.colorRange.x);

  color = vec4(
    (params.mask & 1) == 1 ? base.x : 0.f,
    (params.mask & 2) == 2 ? base.y : 0.f,
    (params.mask & 4) == 4 ? base.z : 0.f,
    (params.mask & 8) == 8 ? base.w : 0.f);
}
