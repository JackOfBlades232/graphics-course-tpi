#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require


layout(location = 0) out vec4 out_fragColor;

layout(push_constant) uniform params_t
{
  mat4 mProjView;
} params;

layout(binding = 0) uniform sampler2D gbufAlbedo;
layout(binding = 1) uniform sampler2D gbufNormal;

layout(binding = 8) uniform sampler2D gbufDepth;

layout(location = 0) in VS_OUT
{
  vec2 texCoord;
} surf;

vec3 depth_and_tc_to_pos(float depth, vec2 tc)
{
  const vec4 cameraToScreen = vec4(2.f * tc - 1.f, depth, 1.f); 
  const vec4 posHom = inverse(params.mProjView) * cameraToScreen;
  return posHom.xyz / posHom.w;
}

void main()
{
  // Unpack gbuffer
  const float depth = texture(gbufDepth, surf.texCoord).x;
  if (depth > 0.9999f)
  {
    out_fragColor = vec4(vec3(0.f), 1.f);
    return;
  }

  const vec4 albedo = texture(gbufAlbedo, surf.texCoord);
  const vec3 normal = texture(gbufNormal, surf.texCoord).xyz;

  const vec3 pos = depth_and_tc_to_pos(depth, surf.texCoord);
  
  // Calculate lighting
  const vec3 wLightPos = vec3(10, 10, 10);
  const vec3 lightColor = vec3(1.0f, 1.0f, 1.0f);

  const vec3 lightDir   = normalize(wLightPos - pos);
  const vec3 diffuse = max(dot(normal, lightDir), 0.0f) * lightColor;
  const float ambient = 0.05;
  out_fragColor = vec4(diffuse + ambient, 1.0f) * albedo;
}
