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

vec2 rotate60(vec2 cs)
{
  return vec2(cs.x * 0.5f - cs.y * 0.866025f, cs.y * 0.5f + cs.x * 0.866025f);
}

vec2 rotate120(vec2 cs)
{
  return vec2(-cs.x * 0.5f - cs.y * 0.866025f, -cs.y * 0.5f + cs.x * 0.866025f);
}

void main(void)
{
  const GrassInstance inst = culledInstances[gl_InstanceIndex];

  const uint vegTypeId = terrainSource.details[GRASS_INST_GET_ID(inst)].vegetationId;
  const TerrainVegetationRule rule = terrainSource.vegetationTypes[vegTypeId];

  float shrinkFactor = GRASS_INST_GET_SF(inst);

  uvec2 dirq = uvec2(inst.dirQuantized >> 16, inst.dirQuantized & 0xFFFF);
  vec2 dir = (vec2(dirq) / 65535.f) * 2.f - 1.f;

  uint quadVId = gl_VertexIndex;
  if (gl_VertexIndex >= 12)
  {
    dir = rotate120(dir);
    quadVId -= 12;
  }
  else if (gl_VertexIndex >= 6)
  {
    dir = rotate60(dir);
    quadVId -= 6;
  }

  // quad winding order does not matter cuz we don't face cull
  bool isBottomVertex = quadVId == 0 || quadVId == 1 || quadVId == 4;
  bool isFarVertex = quadVId == 1 || quadVId == 4 || quadVId == 5;

  vec3 pos = inst.pos + vec3(dir.x, 0.f, dir.y) * rule.radius * shrinkFactor * (isFarVertex ? 1.f : -1.f);
  if (isBottomVertex)
    pos.y -= GRASS_SANK_PORTION * rule.height * shrinkFactor; 
  else
    pos.y += (1.f - GRASS_SANK_PORTION) * rule.height * shrinkFactor; 

  if (!isBottomVertex)
    pos += inst.windOffset;

  vec2 tc = vec2(isFarVertex ? 1.f : 0.f, isBottomVertex ? 1.f : 0.f);

#if GBUF_PASS
  vOut.wPos     = pos;
#endif
  vOut.texCoord = tc;
  vOut.matId    = rule.matId;

  gl_Position   = calc_adjusted_viewproj_mat(viewParams, viewData) * vec4(pos, 1.f);
}

#endif
