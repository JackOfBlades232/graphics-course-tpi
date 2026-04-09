#ifndef GRASS_MESH_IMPL_VERT
#define GRASS_MESH_IMPL_VERT

#include "draw.h"
#include "materials.h"
#include "constants.h"
#include "geometry.h"
#include "grass.h"

layout(binding = 0, set = 0) readonly buffer instances_t
{
  GrassInstance culledInstances[];
};

layout(binding = 7, set = 0) uniform terrain_source_t
{
  TerrainSourceData terrainSource;
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

layout(location = 0) out VS_OUT
{
#if GBUF_PASS
  vec3 wPos;
#endif
  vec2 texCoord;
  flat uint matId;
} vOut;

out gl_PerVertex { vec4 gl_Position; };

float wind_oscillator(float time, vec2 worldPos)
{
  float phase = dot(worldPos, vec2(0.3, 0.7));
  float w = 0.0;
  w += 0.50 * sin(time * 1.0000 + phase);
  w += 0.25 * sin(time * 2.7183 + phase * 1.7);
  w += 0.10 * sin(time * 7.3891 + phase * 2.3);
  return w;
}

void main(void)
{
  const GrassInstance inst = culledInstances[gl_InstanceIndex];

  const uint vegTypeId = terrainSource.details[GRASS_INSTANCE_ID(inst)].vegetationId;
  const TerrainVegetationRule rule = terrainSource.vegetationTypes[vegTypeId];

  float instDist = length(inst.pos - viewParams.mViewPos);
  float shrinkFactor = min(
    (instDist - constants.vegetationRenderingDistance) /
    (constants.vegetationRenderingDropoffDistance - constants.vegetationRenderingDistance),
    1.f);

  float rotationAngle = GRASS_INSTANCE_ANGLE(inst);
  uint quadVId = gl_VertexIndex;
  if (gl_VertexIndex >= 6)
  {
    rotationAngle += SHADER_PI / 3.f;
    quadVId -= 6;
  }
  if (gl_VertexIndex >= 12)
  {
    rotationAngle += SHADER_PI / 3.f;
    quadVId -= 6;
  }

  // ugh
  vec2 dir = normalize(vec2(cos(rotationAngle), sin(rotationAngle)));

  // quad winding order does not matter cuz we don't face cull
  bool isBottomVertex = quadVId == 0 || quadVId == 1 || quadVId == 4;
  bool isFarVertex = quadVId == 1 || quadVId == 4 || quadVId == 5;
  vec3 pos = inst.pos + vec3(dir.x, 0.f, dir.y) * rule.radius * shrinkFactor * (isFarVertex ? 1.f : -1.f);
  if (isBottomVertex)
    pos.y -= GRASS_SANK_PORTION * rule.height * shrinkFactor; 
  else
    pos.y += (1.f - GRASS_SANK_PORTION) * rule.height * shrinkFactor; 

  if (constants.windStrength >= 0.001f && !isBottomVertex)
  {
    vec2 windVec = pos.xz - constants.windOrigin;
    float dist = length(windVec);

    // @TODO: settable
    const float lambda = 0.5f;
    const float plambda = 0.5f;
    const float pscale = 5.f;
    const float relAmp = 0.4f;

    float strengthFalloff = lambda + (1.f - lambda) / (1.f + dist);
    float strengthPeriod = pscale * (plambda + (1.f - plambda) / (1.f + dist));

    // @TODO: more sine waves?
    float strengthOscillation = relAmp * wind_oscillator(strengthPeriod * constants.time, inst.pos.xz);

    float windInfluenceAmt = constants.windStrength * (1.f + strengthOscillation) * strengthFalloff;
    vec2 windInfluence = (windVec / dist) * windInfluenceAmt;

    vec3 posOffset = shrinkFactor * vec3(windInfluence.x, -0.5f * (windInfluenceAmt * windInfluenceAmt), windInfluence.y);
    pos += posOffset;
  }

  vec2 tc = vec2(isFarVertex ? 1.f : 0.f, isBottomVertex ? 1.f : 0.f);

#if GBUF_PASS
  vOut.wPos     = pos;
#endif

  vOut.texCoord = tc;
  vOut.matId    = rule.matId;

  gl_Position   = calc_adjusted_viewproj_mat(viewParams, viewData) * vec4(pos, 1.f);
}

#endif
