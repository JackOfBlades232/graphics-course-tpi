#pragma once

#include <etna/Assert.hpp>

#include <optional>

template <class T>
inline T unwrap(std::optional<T>&& opt)
{
  ETNA_VERIFY(opt);
  return std::move(*opt);
}

template <class T>
inline T next_pot_pow(T val)
{
  return T(ceilf(log2f(float(val))));
}

template <class T>
inline T next_pot(T val)
{
  return T{1} << next_pot_pow(val);
}

template <class T>
inline bool is_pot(T val)
{
  return val == next_pot(val);
}

template <class T>
inline T align_up_pot(T val, uint32_t alignment)
{
  ETNA_ASSERT(alignment > 0u && is_pot(alignment));
  return (val + T{alignment - 1u}) & ~T{alignment - 1u};
}

template <class T>
inline T align_up_npot(T val, uint32_t alignment)
{
  ETNA_ASSERT(alignment > 0u);
  return ((val - T{1}) / T{alignment} + T{1}) * T{alignment};
}

template <class T>
inline T div_enough(T val, T div)
{
  return (val + div - T{1}) / div;
}
