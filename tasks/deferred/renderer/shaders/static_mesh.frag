#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require


layout(location = 0) out vec4 out_fragAlbedo;
layout(location = 1) out vec3 out_fragNormal;

layout(location = 0) in VS_OUT
{
  vec3 wPos;
  vec3 wNorm;
  vec3 wTangent;
  vec2 texCoord;
} surf;

void main()
{
  const vec3 surfaceColor = vec3(1.f, 1.f, 1.f);

  out_fragAlbedo = vec4(surfaceColor, 1.f);
  out_fragNormal = surf.wNorm;
}
