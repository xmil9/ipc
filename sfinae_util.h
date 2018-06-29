#pragma once
#include <string>
#include <type_traits>


///////////////////
// Detect if a given type is a std::basic_string.

template <typename T>
struct is_string : std::false_type
{
};

template <typename TChar, typename traits, typename Alloc>
struct is_string<std::basic_string<TChar, traits, Alloc>> : std::true_type
{
};
