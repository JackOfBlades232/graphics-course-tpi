#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "draw.h"
#include "terrain.h"


layout(vertices = 4) out;

layout(binding = 1, set = 0) readonly buffer instances_t
{
  DrawableInstance markedInstances[];
};

layout(location = 0) in VS_OUT
{
  flat uint chunkId;
} surf[];

// @TEST
layout(location = 0) patch out TC_OUT
{
  uint isTop;
  uint isBottom;
  uint isLeft;
  uint isRight;
} tcOut;

bool chunk_is_on_top_edge(const uint chunkId)
{
  if (chunkId < TERRAIN_FIRST_LEVEL_CHUNKS)
  {
    return chunkId < TERRAIN_CHUNKS_LEVEL_DIM;
  }
  else
  {
    const uint relId = (chunkId - TERRAIN_FIRST_LEVEL_CHUNKS) % TERRAIN_OTHER_LEVELS_CHUNKS;
    return relId < TERRAIN_CHUNKS_LEVEL_DIM;
  }
}

bool chunk_is_on_bottom_edge(const uint chunkId)
{
  if (chunkId < TERRAIN_FIRST_LEVEL_CHUNKS)
  {
    return chunkId >= TERRAIN_FIRST_LEVEL_CHUNKS - TERRAIN_CHUNKS_LEVEL_DIM;
  }
  else
  {
    const uint relId = (chunkId - TERRAIN_FIRST_LEVEL_CHUNKS) % TERRAIN_OTHER_LEVELS_CHUNKS;
    return relId >= TERRAIN_OTHER_LEVELS_CHUNKS - TERRAIN_CHUNKS_LEVEL_DIM;
  }
}

bool chunk_is_on_left_edge(const uint chunkId)
{
  if (chunkId < TERRAIN_FIRST_LEVEL_CHUNKS)
  {
    return chunkId % TERRAIN_CHUNKS_LEVEL_DIM == 0;
  }
  else
  {
    const uint relId = (chunkId - TERRAIN_FIRST_LEVEL_CHUNKS) % TERRAIN_OTHER_LEVELS_CHUNKS;
    if (relId < TERRAIN_CHUNKS_LEVEL_DIM || relId >= TERRAIN_OTHER_LEVELS_CHUNKS - TERRAIN_CHUNKS_LEVEL_DIM)
      return relId % TERRAIN_CHUNKS_LEVEL_DIM == 0;
    else
      return relId % 2 == 1;
  }
}

bool chunk_is_on_right_edge(const uint chunkId)
{
  if (chunkId < TERRAIN_FIRST_LEVEL_CHUNKS)
  {
    return chunkId % TERRAIN_CHUNKS_LEVEL_DIM == TERRAIN_CHUNKS_LEVEL_DIM - 1;
  }
  else
  {
    const uint relId = (chunkId - TERRAIN_FIRST_LEVEL_CHUNKS) % TERRAIN_OTHER_LEVELS_CHUNKS;
    if (relId < TERRAIN_CHUNKS_LEVEL_DIM || relId >= TERRAIN_OTHER_LEVELS_CHUNKS - TERRAIN_CHUNKS_LEVEL_DIM)
      return relId % TERRAIN_CHUNKS_LEVEL_DIM == TERRAIN_CHUNKS_LEVEL_DIM - 1;
    else
      return relId % 2 == 0;
  }
}

void main(void)
{
  gl_out[gl_InvocationID].gl_Position = gl_in[gl_InvocationID].gl_Position;

  if (gl_InvocationID == 0)
  {
    const uint chunkId = surf[0].chunkId;
    
    // @TODO: might be good to try larger space for outer tris
    gl_TessLevelOuter[0] =
      chunk_is_on_left_edge(chunkId) ?
      float(TERRAIN_CHUNK_TESSELLATION_FACTOR / 2) :
      float(TERRAIN_CHUNK_TESSELLATION_FACTOR);
    gl_TessLevelOuter[1] =
      chunk_is_on_top_edge(chunkId) ?
      float(TERRAIN_CHUNK_TESSELLATION_FACTOR / 2) :
      float(TERRAIN_CHUNK_TESSELLATION_FACTOR);
    gl_TessLevelOuter[2] =
      chunk_is_on_right_edge(chunkId) ?
      float(TERRAIN_CHUNK_TESSELLATION_FACTOR / 2) :
      float(TERRAIN_CHUNK_TESSELLATION_FACTOR);
    gl_TessLevelOuter[3] =
      chunk_is_on_bottom_edge(chunkId) ?
      float(TERRAIN_CHUNK_TESSELLATION_FACTOR / 2) :
      float(TERRAIN_CHUNK_TESSELLATION_FACTOR);

    gl_TessLevelInner[0] = TERRAIN_CHUNK_TESSELLATION_FACTOR;
    gl_TessLevelInner[1] = TERRAIN_CHUNK_TESSELLATION_FACTOR;

    // @TEST
    tcOut.isTop = chunk_is_on_top_edge(chunkId) ? 1 : 0;
    tcOut.isBottom = chunk_is_on_bottom_edge(chunkId) ? 1 : 0;
    tcOut.isLeft = chunk_is_on_left_edge(chunkId) ? 1 : 0;
    tcOut.isRight = chunk_is_on_right_edge(chunkId) ? 1 : 0;
  }
}
