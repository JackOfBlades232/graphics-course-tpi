#version 450
#extension GL_GOOGLE_include_directive : require
#extension GL_ARB_separate_shader_objects : enable

#include "materials.h"
#include "water.h"
#include "constants.h"
#include "quantization.h"


// @TODO: find an appropriate way to unify with static_mesh

layout(location = 0) out vec4 out_fragAlbedo;
layout(location = 1) out vec3 out_fragMaterial;
layout(location = 2) out vec3 out_fragNormal;
layout(location = 3) out vec4 out_fragTransmission;
layout(location = 4) out vec2 out_motionVector;

layout(binding = 3, set = 0) uniform sampler2D derivatives[WATER_CASCADE_COUNT];
layout(binding = 4, set = 0) uniform sampler2D turbulence[WATER_CASCADE_COUNT];

layout(binding = 6, set = 0) uniform water_source_t
{
  WaterSourceData source;
};
layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};
layout(binding = 9, set = 0) uniform view_params_t
{
  ViewParams viewParams;
};
layout(binding = 10, set = 0) readonly buffer view_data_t
{
  ViewData viewData;
};

layout(location = 0) in TE_OUT
{
  vec3 wPos;
  vec2 wInitPlanarPos;
} surf;

#include "motion_vectors.glsl.inc"

struct Cascade
{
  float dydx;
  float dydz;
  float dxdx;
  float dzdz;
  float turbulence;
};

Cascade sample_cascade(vec2 world_planar_pos, uint cid)
{
  float l = 0.f;
  switch (cid)
  {
  case 0:  l = source.l0; break;
  case 1:  l = source.l1; break;
  case 2:  l = source.l2; break;
  default:                break;
  }

  vec2 uv = world_planar_pos / l;

  Cascade data;
  vec4 derivatives = textureLod(derivatives[cid], uv, 0.f);
  data.dydx = derivatives.x;
  data.dydz = derivatives.y;
  data.dxdx = derivatives.z;
  data.dzdz = derivatives.w;
  data.turbulence = textureLod(turbulence[cid], uv, 0.f).x;

  return data;
}

// @TEST
float K(uint cid)
{
  return 100.f * pow(10.f, cid);
}

void main(void)
{
  vec4 surfaceColor;
  vec3 materialData;
  vec3 normal;

  float dydx = 0.f;
  float dydz = 0.f;
  float dxdx = 0.f;
  float dzdz = 0.f;
  float turbulence = 0.f;

  // @TEST
  for (uint cid = 0; cid < WATER_CASCADE_COUNT; ++cid)
  {
    Cascade data = sample_cascade(surf.wInitPlanarPos, cid);
    dydx += K(cid) * data.dydx;
    dydz += K(cid) * data.dydz;
    dxdx += K(cid) * data.dxdx;
    dzdz += K(cid) * data.dzdz;
    turbulence += data.turbulence - 1.f;
  }

  const vec2 slope = vec2(dydx / abs(1.f + dxdx), dydz / abs(1.f + dzdz));
  const vec3 wNormal = normalize(vec3(-slope.x, 1.f, -slope.y));

  // @TEST
  const float foamfactor = 0.f;
  //const float foamfactor = turbulence + 0.002f;

  // @TEST
  vec4 waterColor = vec4(0.f, 0.4f, 1.f, 1.f);
  vec3 waterMatdata = vec3(float(MATERIAL_PBR), 0.f, 0.2f);

  vec4 foamColor = vec4(1.f, 1.f, 1.f, 1.f);
  vec3 foamMatdata = vec3(float(MATERIAL_PBR), 0.f, 0.9f);

  surfaceColor = foamfactor < 0.f ? foamColor : waterColor;
  materialData = foamfactor < 0.f ? foamMatdata : waterMatdata;
  normal = wNormal;

  out_fragAlbedo = surfaceColor;
  out_fragMaterial = materialData;
  out_fragNormal = normal;
  out_fragTransmission = vec4(vec3(1.f), 0.f);

  vec4 prevNdc = calc_prev_adjusted_viewproj_mat(viewParams, viewData) * vec4(surf.wPos, 1.f);
  vec2 prevNdcXy = prevNdc.xy / prevNdc.w;

  get_static_pixel_motion_vector(
    gl_FragCoord.xy,
    constants.mainTargetResolution,
    prevNdcXy,
    get_subpixel_uv_jitter(viewParams),
    viewParams.prevSubpixelUvJitter,
    out_motionVector);
}

