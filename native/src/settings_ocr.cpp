#include "settings_ocr.hpp"

#include "language_catalog.hpp"

#include <algorithm>

namespace minitrans::settings_ocr {

std::wstring language_name(std::wstring_view tag) {
    // OCR reports BCP-47 tags such as "zh-Hans-CN"; walk back to the longest
    // prefix the language catalog knows so the labels live in one place.
    for (std::wstring_view candidate = tag;; candidate = candidate.substr(0, candidate.rfind(L'-'))) {
        if (const LanguageRow* row = find_language(candidate)) {
            return std::wstring(row->display);
        }
        if (candidate == L"it") return L"意大利语";
        if (candidate == L"pt") return L"葡萄牙语";
        if (candidate.rfind(L'-') == std::wstring_view::npos) break;
    }
    return std::wstring(tag);
}

std::vector<Row> build_rows(const std::vector<std::wstring>& installed) {
    std::vector<Row> rows;
    if (installed.empty()) {
        rows.push_back(Row{{L"zh-Hans-CN", L"en-US"}, L"未检测到 Windows OCR 语言包"});
        return rows;
    }
    rows.push_back(Row{installed, L"自动（推荐）"});
    for (const auto& tag : installed) {
        rows.push_back(Row{{tag}, L"只用" + language_name(tag)});
    }
    return rows;
}

std::size_t find_row(const std::vector<Row>& rows, const std::vector<std::wstring>& saved) {
    for (std::size_t index = 0; index < rows.size(); ++index) {
        if (rows[index].languages == saved) return index;
    }
    return not_found;
}

Row saved_row(const std::vector<std::wstring>& saved) {
    std::wstring label = L"当前设置（";
    for (const auto& language : saved) {
        if (label.size() > 5) label += L"、";
        label += language_name(language);
    }
    label += L"）";
    return Row{saved, label};
}

std::wstring installed_summary(const std::vector<std::wstring>& installed) {
    std::wstring summary = L"系统已安装：";
    if (installed.empty()) {
        return summary + L"无";
    }
    for (std::size_t index = 0; index < installed.size(); ++index) {
        if (index) summary += L"、";
        summary += language_name(installed[index]);
    }
    return summary;
}

std::size_t clamp_selection(int index, std::size_t size) {
    if (size == 0 || index < 0) return 0;
    const std::size_t value = static_cast<std::size_t>(index);
    return value < size ? value : size - 1;
}

}  // namespace minitrans::settings_ocr
