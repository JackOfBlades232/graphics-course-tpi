#pragma once

#include <etna/Assert.hpp>

#include <optional>

template <class T>
T unwrap(std::optional<T>&& opt)
{
  ETNA_VERIFY(opt);
  return std::move(*opt);
}
