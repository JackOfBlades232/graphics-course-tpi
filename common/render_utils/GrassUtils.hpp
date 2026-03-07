#pragma once

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include <vector>
#include <random>

struct GrassChunkTemplate
{
  std::vector<glm::vec2> planarPositions;
};

inline GrassChunkTemplate generate_grass_chunk_template(
  float sparseness_radius, float chunk_size, int stop_iterations = 500)
{
  GrassChunkTemplate chunk{};

  std::mt19937_64 rng{std::random_device{}()};
  std::uniform_real_distribution<float> samp{0.0, chunk_size};

  for (;;)
  {
    int iter = stop_iterations;

    while (iter--)
    {
      glm::vec2 pos{samp(rng), samp(rng)};
      for (auto const& other : chunk.planarPositions)
      {
        if (glm::length(pos - other) < sparseness_radius)
          goto go_on;
      }

      chunk.planarPositions.push_back(pos);
      spdlog::info(
        "chunksize {}, stopped at iter {}",
        chunk.planarPositions.size(),
        stop_iterations - iter - 1);
      break;

    go_on:
      continue;
    }

    if (iter == 0)
      break;
  }

  return chunk;
}
