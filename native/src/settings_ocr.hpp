#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace minitrans::settings_ocr {

// Row indexes come back as this when the stored set is not offered.
inline constexpr std::size_t not_found = static_cast<std::size_t>(-1);

// One row of the "识别语言" combo. `languages` is what the row writes back into
// ocr.languages: the whole installed set for "自动", a single tag for "只用X".
struct Row {
    std::vector<std::wstring> languages;
    std::wstring label;
};

// Human-readable name for an OCR engine language tag ("zh-Hans-CN" ->
// "简体中文"), falling back to the tag itself.
[[nodiscard]] std::wstring language_name(std::wstring_view tag);

// Rows for the installed engines: one "自动（推荐）" row that uses all of them,
// then one "只用X" row per engine. With no engine the combo degrades to a
// single placeholder row that keeps the default set.
[[nodiscard]] std::vector<Row> build_rows(const std::vector<std::wstring>& installed);

// The row whose stored set equals `saved` in order, or not_found when the saved
// set is no longer offered (a language pack may have been removed).
[[nodiscard]] std::size_t find_row(const std::vector<Row>& rows,
                                   const std::vector<std::wstring>& saved);

// The "当前设置（…）" row appended so a saved set that is no longer offered can
// still be shown and stay selected.
[[nodiscard]] Row saved_row(const std::vector<std::wstring>& saved);

// "系统已安装：简体中文、英语" for the note under the combo.
[[nodiscard]] std::wstring installed_summary(const std::vector<std::wstring>& installed);

// Index to read the combo at; clamps the -1 an empty combo reports.
[[nodiscard]] std::size_t clamp_selection(int index, std::size_t size);

}  // namespace minitrans::settings_ocr
