#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "draw.h"
#include "terrain.h"
#include "constants.h"


layout(quads, equal_spacing, ccw) in;

layout(binding = 8, set = 0) uniform constants_t
{
  Constants constants;
};

#include "terrain_mesh.glsl.inc"

layout(location = 0) out TE_OUT
{
  vec3 wPos;
  vec2 texCoord;
} teOut;

void main(void)
{
  const vec2 baseXZ = gl_in[0].gl_Position.xz;
  const vec2 extentXZ = gl_in[2].gl_Position.xz - baseXZ;

  const vec2 pointXZ = baseXZ + gl_TessCoord.xy * extentXZ;

  const vec2 wOffsetFromClipmapCenter =
    pointXZ - constants.lastToroidalUpdatePlayerWorldPos.xz;

  teOut.wPos = vec3(pointXZ.x, sample_geom_clipmap(wOffsetFromClipmapCenter), pointXZ.y);
  teOut.texCoord = wOffsetFromClipmapCenter; // Special for clipmap sampling

  gl_Position = constants.mProjView * vec4(teOut.wPos, 1.0);
}
