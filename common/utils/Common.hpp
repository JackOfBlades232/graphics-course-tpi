#pragma once

#include <etna/Assert.hpp>

#include <optional>
#include <string>
#include <concepts>

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

template <std::floating_point T>
inline T snap_down(T v, T cell)
{
  return floor(v / cell) * cell;
}

template <std::floating_point T>
inline T snap_up(T v, T cell)
{
  return ceil(v / cell) * cell;
}

template <std::floating_point T>
T lerp(T a, T b, T f)
{
  return a + f * (b - a);
}

template <class TS>
  requires(std::same_as<TS, std::string> || std::same_as<TS, std::wstring>)
std::string to_char_str(const TS& s)
{
  if constexpr (std::same_as<TS, std::string>)
    return s;
  else
    return std::to_string(s);
}

namespace detail
{

template <class F>
struct Defer
{
  F f;

  Defer(F&& a_f)
    : f(std::move(a_f))
  {
  }
  ~Defer() { f(); }
};

template <class T, size_t N, size_t... Is, class F>
static std::array<T, N> array_make_impl(std::index_sequence<Is...>, F&& make)
{
  return {((void)Is, make())...};
}

} // namespace detail

template <class T, size_t N, class F>
static std::array<T, N> array_make(F&& make)
{
  return detail::array_make_impl<T, N>(std::make_index_sequence<N>{}, std::forward<F>(make));
}

#define DEFER(f_)                                                                                  \
  detail::Defer defer##__COUNTER__                                                                 \
  {                                                                                                \
    f_                                                                                             \
  }

#define VARIANT_IS(v_, t_) (std::is_same_v<std::remove_cvref_t<decltype(v_)>, t_>)

#define ARRCNT(a_) (sizeof(a_) / sizeof((a_)[0]))
#define ARRCNT2(a_) (sizeof(a_) / sizeof((a_)[0][0]))
