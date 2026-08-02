#pragma once

#include <string>
#include <string_view>
#include <vector>

/** Convenience alias for a dynamically-sized array of elements. */
template <typename T>
using Vec = std::vector<T>;

/** Convenience alias for std::string. */
using str = std::string;

/** Convenience alias for std::string_view (non-owning view of a string). */
using str_view = std::string_view;
