#pragma once

#include <array>
#include <cstddef>
#include <string_view>
#include <utility>

namespace minitrans {

// Single source of truth for the languages MiniTranslator supports.
//
// Every list the UI shows is derived from this table at compile time, so adding
// a language is one row here plus its provider code mapping — the drop-downs,
// validation and prompt names follow automatically. `chinese_target` marks the
// rows offered as the target when the source text is Chinese: zh-Hans is left
// out because Chinese input already becomes Simplified Chinese, while the
// per-block target menu uses the whole table.
struct LanguageRow {
    std::wstring_view code;
    std::wstring_view display;
    std::wstring_view prompt;
    bool chinese_target;
};

inline constexpr std::array<LanguageRow, 9> language_catalog{{
    {L"zh-Hans", L"简体中文", L"Simplified Chinese", false},
    {L"zh-Hant", L"繁体中文", L"Traditional Chinese", true},
    {L"en", L"英语", L"English", true},
    {L"ja", L"日语", L"Japanese", true},
    {L"ko", L"韩语", L"Korean", true},
    {L"fr", L"法语", L"French", true},
    {L"de", L"德语", L"German", true},
    {L"es", L"西班牙语", L"Spanish", true},
    {L"ru", L"俄语", L"Russian", true},
}};

[[nodiscard]] constexpr const LanguageRow* find_language(std::wstring_view code) noexcept {
    for (const auto& row : language_catalog) {
        if (row.code == code) return &row;
    }
    return nullptr;
}

constexpr std::size_t chinese_target_count() noexcept {
    std::size_t count = 0;
    for (const auto& row : language_catalog) {
        if (row.chinese_target) ++count;
    }
    return count;
}

// Combo-box contents as (code, label) pairs, in catalog order.
constexpr auto make_language_choices() {
    std::array<std::pair<std::wstring_view, std::wstring_view>, language_catalog.size()> rows{};
    for (std::size_t index = 0; index < language_catalog.size(); ++index) {
        rows[index] = {language_catalog[index].code, language_catalog[index].display};
    }
    return rows;
}

constexpr auto make_chinese_language_choices() {
    std::array<std::pair<std::wstring_view, std::wstring_view>, chinese_target_count()> rows{};
    std::size_t index = 0;
    for (const auto& row : language_catalog) {
        if (row.chinese_target) rows[index++] = {row.code, row.display};
    }
    return rows;
}

constexpr auto make_language_codes() {
    std::array<std::wstring_view, language_catalog.size()> codes{};
    for (std::size_t index = 0; index < language_catalog.size(); ++index) {
        codes[index] = language_catalog[index].code;
    }
    return codes;
}

inline constexpr auto language_choices = make_language_choices();
inline constexpr auto chinese_language_choices = make_chinese_language_choices();
inline constexpr auto known_language_codes = make_language_codes();

}  // namespace minitrans
