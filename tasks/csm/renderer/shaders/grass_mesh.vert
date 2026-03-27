#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

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
  vec3 wPos;
  vec3 wNorm;
  vec4 wTangent;
  vec2 texCoord;
  flat uint matId;
} vOut;

out gl_PerVertex { vec4 gl_Position; };

void main(void)
{
  const GrassInstance inst = culledInstances[gl_InstanceIndex];

  const uint vegTypeId = terrainSource.details[GRASS_INSTANCE_ID(inst)].vegetationId;
  const TerrainVegetationRule rule = terrainSource.vegetationTypes[vegTypeId];

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
  vec3 pos = inst.pos + vec3(dir.x, 0.f, dir.y) * rule.radius * (isFarVertex ? 1.f : -1.f);
  if (isBottomVertex)
    pos.y -= GRASS_SANK_PORTION * rule.height; 
  else
    pos.y += (1.f - GRASS_SANK_PORTION) * rule.height; 

  // @TODO: sort this space out (has to match both sides)
  vec3 norm = vec3(-dir.y, 0.f, dir.x);
  vec4 tang = vec4(dir.x, 0.f, dir.y, 1.f);
  vec2 tc = vec2(isFarVertex ? 1.f : 0.f, isBottomVertex ? 1.f : 0.f);

  vOut.wPos     = pos;
  vOut.wNorm    = norm;
  vOut.wTangent = tang;
  vOut.texCoord = tc;
  vOut.matId    = rule.matId;

  gl_Position   = calc_adjusted_viewproj_mat(viewParams, viewData) * vec4(vOut.wPos, 1.f);
}
