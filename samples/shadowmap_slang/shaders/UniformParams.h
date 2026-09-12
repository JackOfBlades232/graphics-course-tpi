#ifndef UNIFORM_PARAMS_H_INCLUDED
#define UNIFORM_PARAMS_H_INCLUDED

#ifdef __cplusplus
using shader_int = int;
using shader_ivec2 = glm::ivec2;
using shader_ivec3 = glm::ivec3;
using shader_uint = glm::uint;
using shader_uvec2 = glm::uvec2;
using shader_uvec3 = glm::uvec3;
using shader_float = float;
using shader_vec2 = glm::vec2;
using shader_vec3 = glm::vec3;
using shader_vec4 = glm::vec4;
using shader_mat4x3 = glm::mat4x3;
using shader_mat4 = glm::mat4x4;
using shader_bool = glm::uint;
#else
#define shader_int int
#define shader_ivec2 int4
#define shader_ivec3 int3
#define shader_uint uint
#define shader_uvec2 uint2
#define shader_uvec3 uint3
#define shader_float float
#define shader_vec2 float2
#define shader_vec3 float3
#define shader_vec4 float4
#define shader_mat4x3 float4x3
#define shader_mat4 float4x4
#define shader_bool bool
#endif

struct UniformParams
{
  shader_mat4 lightMatrix;
  shader_vec3 lightPos;
  shader_float time;
  shader_vec3 baseColor;
};

#endif // UNIFORM_PARAMS_H_INCLUDED
