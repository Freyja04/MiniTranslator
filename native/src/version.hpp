#pragma once

#include <string_view>

#ifndef MINITRANS_VERSION_WIDE
#define MINITRANS_VERSION_WIDE L"0.0.0"
#endif

namespace minitrans {

inline constexpr std::wstring_view native_version = MINITRANS_VERSION_WIDE;

}  // namespace minitrans
