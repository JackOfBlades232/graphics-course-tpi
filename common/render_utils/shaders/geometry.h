#ifndef GEOMETRY_H_INCLUDED
#define GEOMETRY_H_INCLUDED

#include "cpp_glsl_compat.h"
#include "lights.h"

#ifdef __cplusplus

enum class ViewType : shader_uint
{
  PERSPECTIVE = 0,
  ORTHO = 1
};

enum class CullingMode : shader_uint
{
  PER_VERTEX = 0,
  SAT = 1
};

#define VIEW_TYPE_PERSPECTIVE ViewType::PERSPECTIVE
#define VIEW_TYPE_ORTHO ViewType::ORTHO

#define CULLING_MODE_PER_VERTEX CullingMode::PER_VERTEX
#define CULLING_MODE_SAT CullingMode::SAT

#else

#define ViewType shader_uint
#define VIEW_TYPE_PERSPECTIVE 0
#define VIEW_TYPE_ORTHO 1

#define CullingMode shader_uint
#define CULLING_MODE_PER_VERTEX 0
#define CULLING_MODE_SAT 1

#endif

struct BBox
{
  shader_vec4 min;
  shader_vec4 max;
};

struct OBBox
{
  shader_vec3 center;
  shader_vec3 extents;
  shader_vec3 xAxis;
  shader_vec3 yAxis;
  shader_vec3 zAxis;
};

struct ViewFrustum
{
  shader_float nearX;
  shader_float nearY;
  shader_float nearZ;
  shader_float farZ;
};

struct ViewParams
{
  shader_mat4 mProjView;
  shader_mat4 mView;
  shader_mat4 mInverseView;
  shader_mat4 mProj;
  shader_mat4 mPrevProjView;
  shader_mat4 mPrevView;
  shader_mat4 mPrevProj;
  ViewFrustum viewFrustum;
  ViewFrustum prevViewFrustum;
  shader_vec3 viewPos;
  float subpixelUvJitterX;
  shader_vec3 prevViewPos;
  float subpixelUvJitterY;
  // Workaround for a bug where [1] array is probably flattened
  shader_vec4 csmFrustumSplits[SHADER_MAX((CSM_CASCADE_COUNT - 1) / 4 + 1, 2)];
  ViewType type;
  shader_uint needDepthBounds;
  shader_uint needReverseZ;
  shader_uint pad2_;
  shader_vec2 prevSubpixelUvJitter;
  shader_vec2 pad3_;
  shader_vec3 viewDir;
  shader_uint pad4_;
};

shader_inline shader_vec2 get_subpixel_uv_jitter(ViewParams params)
{
  return shader_vec2(params.subpixelUvJitterX, params.subpixelUvJitterY);
}

// Calculated on the GPU
struct ViewData
{
  shader_uint minViewZOrderedUint;
  shader_uint maxViewZOrderedUint;
  shader_uint prevMinViewZOrderedUint;
  shader_uint prevMaxViewZOrderedUint;
};

#define CALC_DEPTH_BOUNDS_ELEMS_PER_THREAD 32

shader_inline shader_mat4 patch_depth_bounds_persp(shader_mat4 pm, float znear, float zfar)
{
  const float invZRng = 1.f / (zfar - znear);
  pm[2][2] = zfar * invZRng;
  pm[3][2] = -znear * zfar * invZRng;
  return pm;
}

shader_inline shader_mat4 patch_depth_bounds_ortho(shader_mat4 pm, float znear, float zfar)
{
  const float invZRng = 1.f / (zfar - znear);
  pm[2][2] = invZRng;
  pm[3][2] = -znear * invZRng;
  return pm;
}

#ifndef __cplusplus

float get_frustum_split(in ViewParams p, uint i)
{
  uint comp = i & 3;
  if (comp == 0)
    return p.csmFrustumSplits[i & ~3].x;
  else if (comp == 1)
    return p.csmFrustumSplits[i & ~3].y;
  else if (comp == 2)
    return p.csmFrustumSplits[i & ~3].z;
  else
    return p.csmFrustumSplits[i & ~3].w;
}

vec2 get_corrected_depth_bounds_impl(float n, float f, uint minord, uint maxord)
{
  float sceneMinZ = ordered_uint_to_float(minord) - 0.001f;
  float sceneMaxZ = ordered_uint_to_float(maxord) + 0.001f;
  float correctedNearZ = max(sceneMinZ, n);
  float correctedFarZ = min(sceneMaxZ, f);
  return vec2(correctedNearZ, correctedFarZ);
}

vec2 get_corrected_depth_bounds(in ViewParams params, in ViewData data, bool prev)
{
  vec2 nf;
  if (prev)
  {
    if (params.needDepthBounds == 0)
      nf = vec2(params.prevViewFrustum.nearZ, params.prevViewFrustum.farZ);
    else
    {
      nf = get_corrected_depth_bounds_impl(
        params.prevViewFrustum.nearZ, params.prevViewFrustum.farZ,
        data.prevMinViewZOrderedUint, data.prevMaxViewZOrderedUint);
    }
  }
  else
  {
    if (params.needDepthBounds == 0)
      nf = vec2(params.viewFrustum.nearZ, params.viewFrustum.farZ);
    else
    {
      nf = get_corrected_depth_bounds_impl(
        params.viewFrustum.nearZ, params.viewFrustum.farZ,
        data.minViewZOrderedUint, data.maxViewZOrderedUint);
    }
  }
  return nf;
}

mat4 calc_adjusted_viewproj_mat(in ViewParams params, in ViewData data)
{
  if (params.needDepthBounds == 0)
    return params.mProjView;
  const vec2 zNearFar = get_corrected_depth_bounds(params, data, false);
  const mat4 pm = params.mProj;
  const float znr = params.needReverseZ != 0 ? zNearFar.y : zNearFar.x;
  const float zfr = params.needReverseZ != 0 ? zNearFar.x : zNearFar.y;
  mat4 adjPm;
  if (params.type == VIEW_TYPE_PERSPECTIVE)
    adjPm = patch_depth_bounds_persp(pm, znr, zfr);
  else // VIEW_TYPE_ORTHO
    adjPm = patch_depth_bounds_ortho(pm, znr, zfr);
  return adjPm * params.mView;
}

mat4 calc_prev_adjusted_viewproj_mat(in ViewParams params, in ViewData data)
{
  if (params.needDepthBounds == 0)
    return params.mPrevProjView;
  const vec2 zNearFar = get_corrected_depth_bounds(params, data, true);
  const mat4 pm = params.mPrevProj;
  const float znr = params.needReverseZ != 0 ? zNearFar.y : zNearFar.x;
  const float zfr = params.needReverseZ != 0 ? zNearFar.x : zNearFar.y;
  mat4 adjPm;
  if (params.type == VIEW_TYPE_PERSPECTIVE)
    adjPm = patch_depth_bounds_persp(pm, znr, zfr);
  else // VIEW_TYPE_ORTHO
    adjPm = patch_depth_bounds_ortho(pm, znr, zfr);
  return adjPm * params.mPrevView;
}

// @TODO: fix, seems botched

float linearize_z(float z, in ViewParams params, in ViewData data)
{
  vec2 nf = get_corrected_depth_bounds(params, data, false);
  float n = nf.x;
  float f = nf.y;
  if (params.needReverseZ != 0)
    return (f * n) / (n - z * (n - f));
  else
    return (f * n) / (f - z * (f - n));
}

float linearize_prev_z(float z, in ViewParams params, in ViewData data)
{
  vec2 nf = get_corrected_depth_bounds(params, data, true);
  float n = nf.x;
  float f = nf.y;
  if (params.needReverseZ != 0)
    return (f * n) / (n - z * (n - f));
  else
    return (f * n) / (f - z * (f - n));
}

// @TODO: optimize
vec3 depth_and_tc_to_pos_with_mat(float depth, vec2 tc, mat4 ivpm)
{
  const vec4 cameraToScreen = vec4(2.f * tc - 1.f, depth, 1.f); 
  const vec4 posHom = ivpm * cameraToScreen;
  return posHom.xyz / posHom.w;
}

vec3 refract_vector(vec3 incoming, vec3 normal, float ior)
{
  const float outgoingCosine = dot(normal, -incoming);
  const float outgoingSine = sqrt(1.f - outgoingCosine * outgoingCosine);
  const float incomingSine = outgoingSine / ior;
  const float incomingCosine = sqrt(1.f - incomingSine * incomingSine);
  const vec3 tangent = normalize(normal * dot(incoming, normal) - incoming);
  const vec3 refractedVector = -(normal * incomingCosine + tangent * incomingSine);
  return refractedVector;
}

#endif

#endif // GEOMETRY_H_INCLUDED
