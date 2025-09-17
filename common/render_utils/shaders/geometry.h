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

struct BBoxAsOrdUints
{
  shader_uvec4 min;
  shader_uvec4 max;
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
  ViewFrustum viewFrustum;
  // Workaround for a bug where [1] array is probably flattened
  shader_vec4 csmFrustumSplits[SHADER_MAX((CSM_CASCADE_COUNT - 1) / 4 + 1, 2)];
  ViewType type;
  shader_uint needDepthBounds;
  shader_uint pad1_, pad2_;
};

// Calculated on the GPU
struct ViewData
{
  shader_uint minViewZOrderedUint;
  shader_uint maxViewZOrderedUint;
};

#define CALC_DEPTH_BOUNDS_ELEMS_PER_THREAD 32

shader_inline shader_mat4 patch_depth_bounds_persp(shader_mat4 pv, float znear, float zfar)
{
  const float invZRng = 1.f / (zfar - znear);
  pv[2][2] = zfar * invZRng;
  pv[3][2] = -znear * zfar * invZRng;
  return pv;
}

shader_inline shader_mat4 patch_depth_bounds_ortho(shader_mat4 pv, float znear, float zfar)
{
  const float invZRng = 1.f / (zfar - znear);
  pv[2][2] = invZRng;
  pv[3][2] = -znear * invZRng;
  return pv;
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

vec2 get_corrected_depth_bounds(in ViewParams params, in ViewData data)
{
  float sceneMinZ = ordered_uint_to_float(data.minViewZOrderedUint) - 0.001f;
  float sceneMaxZ = ordered_uint_to_float(data.maxViewZOrderedUint) + 0.001f;
  float correctedNearZ = max(sceneMinZ, params.viewFrustum.nearZ);
  float correctedFarZ = min(sceneMaxZ, params.viewFrustum.farZ);
  return vec2(correctedNearZ, correctedFarZ);
}

mat4 adjust_depth_bounds(mat4 pv, in ViewParams params, in ViewData data)
{
  if (params.needDepthBounds == 0)
    return pv;
  const vec2 zNearFar = get_corrected_depth_bounds(params, data);
  if (params.type == VIEW_TYPE_PERSPECTIVE)
    return patch_depth_bounds_persp(pv, zNearFar.x, zNearFar.y);
  else // VIEW_TYPE_ORTHO
    return patch_depth_bounds_ortho(pv, zNearFar.x, zNearFar.y);
  return pv;
}

#endif

#endif // GEOMETRY_H_INCLUDED
