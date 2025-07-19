#pragma once

#include <etna/Assert.hpp>

#include <optional>

template <class T>
inline T unwrap(std::optional<T>&& opt)
{
  ETNA_VERIFY(opt);
  return std::move(*opt);
}

inline uint32_t next_pot_pow(uint32_t val)
{
  const uint32_t nextPow = uint32_t(ceil(log2f(float(val))));
  return nextPow;
}

inline uint32_t next_pot(uint32_t val)
{
  return 1u << next_pot_pow(val);
}

inline bool is_pot(uint32_t val)
{
  return val == next_pot(val);
}
