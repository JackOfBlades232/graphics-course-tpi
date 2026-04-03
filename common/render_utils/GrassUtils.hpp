#pragma once

#include <glm/glm.hpp>
#include <spdlog/spdlog.h>

#include <vector>
#include <random>
#include <chrono>

struct GrassChunkTemplate
{
  std::vector<glm::vec2> planarPositions;
};

inline GrassChunkTemplate generate_grass_chunk_template(
  float sparseness_radius,
  float chunk_size,
  int stop_iterations = 1000,
  int total_action_budget = 10'000'000)
{
  auto start = std::chrono::high_resolution_clock::now();

  GrassChunkTemplate chunk{};

  std::mt19937_64 rng{std::random_device{}()};
  std::uniform_real_distribution<float> samp{0.0, chunk_size};

  int actions = 0;

  for (;;)
  {
    int iter = stop_iterations;

    while (iter--)
    {
      glm::vec2 pos{samp(rng), samp(rng)};
      for (auto const& other : chunk.planarPositions)
      {
        ++actions;
        if (glm::length(pos - other) < 2.f * sparseness_radius)
          goto go_on;
      }

      chunk.planarPositions.push_back(pos);
      break;

    go_on:
      continue;
    }

    if (iter == 0 || actions >= total_action_budget)
      break;
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<float, std::milli> elapsed = end - start;

  spdlog::info(
    "Generated grass chunk template of size {} in {}ms, total steps {}",
    chunk.planarPositions.size(),
    elapsed.count(),
    actions);

  return chunk;
}
