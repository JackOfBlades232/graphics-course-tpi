#version 450
#extension GL_ARB_separate_shader_objects : enable
#extension GL_GOOGLE_include_directive : require

#include "draw.h"


layout(push_constant) uniform params_t
{
  uint firstLevelChunks;
  uint otherLevelsChunks;
  uint chunksLevelDim;
  uint chunkTessellationFactor;
  uint levelCount;

  // @TODO: figure out why I don't have gl_BaseInstance and use it instead
  uint chunksInstBase;
} params;

layout(vertices = 4) out;

layout(location = 0) in VS_OUT
{
  flat uint chunkId;
} surf[];

bool chunk_is_on_top_edge(const uint chunkId)
{
  if (chunkId < params.firstLevelChunks)
  {
    return chunkId < params.chunksLevelDim;
  }
  else
  {
    const uint relId = (chunkId - params.firstLevelChunks) % params.otherLevelsChunks;
    return relId < params.chunksLevelDim;
  }
}

bool chunk_is_on_bottom_edge(const uint chunkId)
{
  if (chunkId < params.firstLevelChunks)
  {
    return chunkId >= params.firstLevelChunks - params.chunksLevelDim;
  }
  else
  {
    const uint relId = (chunkId - params.firstLevelChunks) % params.otherLevelsChunks;
    return relId >= params.otherLevelsChunks - params.chunksLevelDim;
  }
}

bool chunk_is_on_left_edge(const uint chunkId)
{
  if (chunkId < params.firstLevelChunks)
  {
    return chunkId % params.chunksLevelDim == 0;
  }
  else
  {
    const uint relId = (chunkId - params.firstLevelChunks) % params.otherLevelsChunks;
    if (relId < params.chunksLevelDim || relId >= params.otherLevelsChunks - params.chunksLevelDim)
      return relId % params.chunksLevelDim == 0;
    else
      return relId % 2 == 1;
  }
}

bool chunk_is_on_right_edge(const uint chunkId)
{
  if (chunkId < params.firstLevelChunks)
  {
    return chunkId % params.chunksLevelDim == params.chunksLevelDim - 1;
  }
  else
  {
    const uint relId = (chunkId - params.firstLevelChunks) % params.otherLevelsChunks;
    if (relId < params.chunksLevelDim || relId >= params.otherLevelsChunks - params.chunksLevelDim)
      return relId % params.chunksLevelDim == params.chunksLevelDim - 1;
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

    const bool isLastLevel =
      chunkId >= params.firstLevelChunks &&
      (chunkId - params.firstLevelChunks) / params.otherLevelsChunks + 1 >=
        (params.levelCount - 1);

    if (isLastLevel)
    {
      gl_TessLevelOuter[0] = float(params.chunkTessellationFactor);
      gl_TessLevelOuter[1] = float(params.chunkTessellationFactor);
      gl_TessLevelOuter[2] = float(params.chunkTessellationFactor);
      gl_TessLevelOuter[3] = float(params.chunkTessellationFactor);
      gl_TessLevelInner[0] = float(params.chunkTessellationFactor);
      gl_TessLevelInner[1] = float(params.chunkTessellationFactor);
    }
    else
    {
      // @TODO: might be good to try larger space for outer tris
      gl_TessLevelOuter[0] =
        chunk_is_on_left_edge(chunkId) ?
        float(params.chunkTessellationFactor / 2) :
        float(params.chunkTessellationFactor);
      gl_TessLevelOuter[1] =
        chunk_is_on_top_edge(chunkId) ?
        float(params.chunkTessellationFactor / 2) :
        float(params.chunkTessellationFactor);
      gl_TessLevelOuter[2] =
        chunk_is_on_right_edge(chunkId) ?
        float(params.chunkTessellationFactor / 2) :
        float(params.chunkTessellationFactor);
      gl_TessLevelOuter[3] =
        chunk_is_on_bottom_edge(chunkId) ?
        float(params.chunkTessellationFactor / 2) :
        float(params.chunkTessellationFactor);

      gl_TessLevelInner[0] = params.chunkTessellationFactor;
      gl_TessLevelInner[1] = params.chunkTessellationFactor;
    }
  }
}
