#pragma once

#include <string>
#include <string_view>

namespace minitrans {

struct ScriptCounts {
    int han{};
    int kana{};
    int hangul{};
    int latin{};
    int cyrillic{};
    int other{};
};

ScriptCounts script_counts(std::wstring_view text);
std::wstring detect_language(std::wstring_view text);
std::wstring target_for(std::wstring_view text, std::wstring_view chinese_target = L"en");
// Output language picked on the screenshot page. Chinese source text falls back
// to English when the choice is Chinese, and any other source language falls
// back to Simplified Chinese when the choice would be a no-op.
std::wstring target_for_selection(std::wstring_view text, std::wstring_view selected);
// Target used for Chinese source text under the same selection.
std::wstring chinese_target_for_selection(std::wstring_view selected);
std::wstring target_display_name(std::wstring_view code);
std::wstring target_prompt_name(std::wstring_view code);
bool matches_target(std::wstring_view text, std::wstring_view target);
bool is_cjk(wchar_t value) noexcept;

}  // namespace minitrans
