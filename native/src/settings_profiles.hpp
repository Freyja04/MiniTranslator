#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace minitrans::settings_profiles {

// One user-defined large-model endpoint. The name is the identity that
// translator.ai_selected points at, so it has to stay unique.
struct Profile {
    std::wstring name;
    std::wstring protocol;
    std::wstring base_url;
    std::wstring key;
    std::wstring model;
};

// Row a profile combo should show: the requested name when it exists,
// otherwise the first row, otherwise an empty string. Keeps "the selection was
// deleted or renamed elsewhere" from leaving the page without a selection.
[[nodiscard]] std::wstring resolve_selection(const std::vector<Profile>& list,
                                             std::wstring_view wanted);

// "接口 N" name that no existing row uses yet.
[[nodiscard]] std::wstring next_default_name(const std::vector<Profile>& list);

// Name to keep selected after the row at `removed` was erased: the row that
// slid into its place, or the new last row when the tail was erased.
[[nodiscard]] std::wstring name_after_removal(const std::vector<Profile>& list,
                                              std::size_t removed);

// Rename target for the row at `index`: `wanted` made unique against the other
// rows with a " 2", " 3", ... suffix. An empty request yields an empty string,
// which callers treat as "nothing to rename".
[[nodiscard]] std::wstring unique_rename(const std::vector<Profile>& list,
                                         std::size_t index, std::wstring_view wanted);

// Widen a combo index, which reports -1 when nothing is selected.
[[nodiscard]] std::size_t clamp_index(int index, std::size_t size);

}  // namespace minitrans::settings_profiles
