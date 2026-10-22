#include "language.hpp"

#include "language_catalog.hpp"

#include <algorithm>
#include <cwctype>

namespace minitrans {

namespace {

bool is_han(wchar_t value) noexcept {
    const auto code = static_cast<unsigned>(value);
    return (code >= 0x3400 && code <= 0x4DBF) ||
           (code >= 0x4E00 && code <= 0x9FFF) ||
           (code >= 0xF900 && code <= 0xFAFF);
}

bool is_ascii_letter(wchar_t value) noexcept {
    return (value >= L'A' && value <= L'Z') || (value >= L'a' && value <= L'z');
}

int latin_evidence(std::wstring_view text, int raw_count) {
    int evidence = raw_count;
    std::size_t index = 0;
    while (index < text.size()) {
        if (!is_ascii_letter(text[index])) {
            ++index;
            continue;
        }
        const std::size_t start = index++;
        int letters = 1;
        int uppercase = text[start] >= L'A' && text[start] <= L'Z' ? 1 : 0;
        int lowercase = text[start] >= L'a' && text[start] <= L'z' ? 1 : 0;
        bool code_marks = false;
        while (index < text.size()) {
            const wchar_t value = text[index];
            if (is_ascii_letter(value)) {
                ++letters;
                uppercase += value >= L'A' && value <= L'Z' ? 1 : 0;
                lowercase += value >= L'a' && value <= L'z' ? 1 : 0;
                ++index;
            } else if ((value >= L'0' && value <= L'9') ||
                       value == L'_' || value == L'.' || value == L'/' ||
                       value == L':' || value == L'\\' || value == L'-') {
                code_marks = true;
                ++index;
            } else {
                break;
            }
        }
        const bool camel = uppercase >= 2 && lowercase >= 1;
        const bool touches_han = (start > 0 && is_han(text[start - 1])) ||
                                 (index < text.size() && is_han(text[index]));
        if (camel || code_marks || touches_han) {
            evidence -= std::max(0, letters - 2);
        }
    }
    return std::max(0, evidence);
}

}  // namespace

bool is_cjk(wchar_t value) noexcept {
    const auto code = static_cast<unsigned>(value);
    return (code >= 0x3000 && code <= 0x30FF) ||
           (code >= 0x3400 && code <= 0x9FFF) ||
           (code >= 0xAC00 && code <= 0xD7AF) ||
           (code >= 0xF900 && code <= 0xFAFF) ||
           (code >= 0xFF00 && code <= 0xFFEF);
}

ScriptCounts script_counts(std::wstring_view text) {
    ScriptCounts counts;
    for (const wchar_t value : text) {
        const auto code = static_cast<unsigned>(value);
        if ((code >= 0x3040 && code <= 0x30FF) ||
            (code >= 0x31F0 && code <= 0x31FF)) {
            ++counts.kana;
        } else if ((code >= 0xAC00 && code <= 0xD7AF) ||
                   (code >= 0x1100 && code <= 0x11FF)) {
            ++counts.hangul;
        } else if (is_han(value)) {
            ++counts.han;
        } else if (code >= 0x0400 && code <= 0x04FF) {
            ++counts.cyrillic;
        } else if (is_ascii_letter(value) || (code >= 0x00C0 && code < 0x0250)) {
            ++counts.latin;
        } else if (!std::iswspace(value)) {
            ++counts.other;
        }
    }
    return counts;
}

std::wstring detect_language(std::wstring_view text) {
    const auto counts = script_counts(text);
    if (counts.kana >= 1 && counts.kana * 12 >= counts.han) {
        return L"ja";
    }
    if (counts.hangul >= 1) {
        return L"ko";
    }
    if (counts.cyrillic > counts.latin && counts.cyrillic >= 2) {
        return L"ru";
    }
    const int latin = latin_evidence(text, counts.latin);
    if (counts.han >= 2 && counts.han * 3 >= latin) {
        return L"zh";
    }
    return L"other";
}

std::wstring target_for(std::wstring_view text, std::wstring_view chinese_target) {
    if (detect_language(text) != L"zh") {
        return L"zh-Hans";
    }
    const LanguageRow* requested = find_language(chinese_target);
    if (requested && requested->chinese_target) {
        return std::wstring(requested->code);
    }
    return L"en";
}

bool is_chinese_code(const LanguageRow* row) {
    return row && (row->code == L"zh-Hans" || row->code == L"zh-Hant");
}

std::wstring target_for_selection(std::wstring_view text, std::wstring_view selected) {
    const LanguageRow* chosen = find_language(selected);
    if (!chosen) {
        return target_for(text, L"en");
    }
    const std::wstring detected = detect_language(text);
    // A Chinese selection would be a no-op for Chinese text and would lose the
    // meaning of every other language, so both fall back to a real translation.
    if (detected == L"zh" && is_chinese_code(chosen)) {
        return L"en";
    }
    if (std::wstring_view(detected) == chosen->code) {
        return L"zh-Hans";
    }
    return std::wstring(chosen->code);
}

std::wstring chinese_target_for_selection(std::wstring_view selected) {
    const LanguageRow* chosen = find_language(selected);
    if (!chosen || is_chinese_code(chosen)) {
        return L"en";
    }
    return std::wstring(chosen->code);
}

std::wstring target_display_name(std::wstring_view code) {
    const LanguageRow* row = find_language(code);
    return row ? std::wstring(row->display) : std::wstring(code);
}

std::wstring target_prompt_name(std::wstring_view code) {
    const LanguageRow* row = find_language(code);
    return row ? std::wstring(row->prompt) : std::wstring(code);
}

bool matches_target(std::wstring_view text, std::wstring_view target) {
    const auto counts = script_counts(text);
    const int cjk = counts.han + counts.kana + counts.hangul;
    if (target == L"en" || target == L"fr" || target == L"de" || target == L"es" ||
        target == L"it" || target == L"pt" || target == L"nl" || target == L"vi") {
        return !(cjk >= 1 && cjk * 2 >= counts.latin);
    }
    const int letters = cjk + counts.latin + counts.cyrillic;
    if (letters == 0) {
        return true;
    }
    if (target == L"zh-Hans" || target == L"zh-Hant") {
        return counts.han >= 1 || (letters == 1 && counts.latin == 1);
    }
    if (target == L"ja") {
        return counts.kana >= 1 || counts.han >= 1 || (letters == 1 && counts.latin == 1);
    }
    if (target == L"ko") {
        return counts.hangul >= 1 || (letters == 1 && counts.latin == 1);
    }
    if (target == L"ru") {
        return counts.cyrillic >= 1 || (letters == 1 && counts.latin == 1);
    }
    return true;
}

}  // namespace minitrans
