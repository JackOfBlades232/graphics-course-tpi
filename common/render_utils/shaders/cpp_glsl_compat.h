#ifndef SHADER_COMMON_H_INCLUDED
#define SHADER_COMMON_H_INCLUDED

// NOTE: .h extension is used for files that can be included both into GLSL and into C++
// GLSL-C++ datatype compatibility layer

#ifdef __cplusplus

#include <glm/glm.hpp>


// NOLINTBEGIN

// NOTE: This is technically completely wrong,
// as GLSL words are guaranteed to be 32-bit,
// while C++ unsigned int can be 16-bit.
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

// The funny thing is, on a GPU, you might as well consider
// a single byte to be 32 bits, because nothing can be smaller
// than 32 bits, so a bool has to be 32 bits as well.
using shader_bool = glm::uint;

#define shader_inline inline

#define shader_clamp glm::clamp
#define shader_floor glm::floor
#define shader_ceil glm::ceil
#define shader_round glm::round
#define shader_sign glm::sign
#define shader_abs glm::abs

#else

#define shader_int int
#define shader_ivec2 ivec2
#define shader_ivec3 ivec3
#define shader_uint uint
#define shader_uvec2 uvec2
#define shader_uvec3 uvec3

#define shader_float float
#define shader_vec2 vec2
#define shader_vec3 vec3
#define shader_vec4 vec4
#define shader_mat4x3 mat4x3
#define shader_mat4 mat4

#define shader_bool bool

#define shader_inline

#define shader_clamp clamp
#define shader_floor floor
#define shader_ceil ceil
#define shader_round round
#define shader_sign sign
#define shader_abs abs

// @TODO: route to glm::translate for cpp
shader_mat4 translation(shader_vec3 offs)
{
  shader_mat4 m = shader_mat4(1.f);
  m[3] = shader_vec4(offs, 1.f);
  return m;
}

#endif

#define round_from_zero(x_) (shader_round((x_) + shader_sign(x_) * 0.5f))
#define round_to_zero(x_) (shader_round((x_) - shader_sign(x_) * 0.5f))

#define SHADER_EPSILON 0.00001f

// NOLINTEND

#endif // SHADER_COMMON_H_INCLUDED
