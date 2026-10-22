#include "core_self_test.hpp"

#include "hotkey.hpp"
#include "http.hpp"
#include "language.hpp"
#include "settings_ocr.hpp"
#include "settings_profiles.hpp"
#include "settings_provider.hpp"
#include "settings_window_internal.hpp"
#include "util.hpp"

#include <algorithm>
#include <array>
#include <set>
#include <string>
#include <string_view>

namespace minitrans {
namespace {

// Every check here stays window-free on purpose: this runs before AppHost
// creates a window station or loads Direct2D, so a failure is reported as a
// plain self-test error on any machine.
void check(bool condition, const char* message) {
    if (!condition) throw AppError(message);
}

void check_language_detection() {
    check(detect_language(L"这是一段简体中文，用来确认语言识别。") == L"zh",
          "core self-test did not detect Chinese text");
    // Identifiers and URLs next to Chinese must not be counted as Latin prose.
    check(detect_language(L"调用 GetProcAddress 之后返回值") == L"zh",
          "core self-test counted code identifiers as Latin text");
    check(detect_language(L"これは日本語のテストです。") == L"ja",
          "core self-test did not detect Japanese text");
    check(detect_language(L"이것은 한국어 테스트입니다.") == L"ko",
          "core self-test did not detect Korean text");
    check(detect_language(L"Это тест на русском языке.") == L"ru",
          "core self-test did not detect Russian text");
    check(detect_language(L"Hello, this is an English sentence.") == L"other",
          "core self-test reported an English sentence as Chinese");
}

void check_language_catalog() {
    check(language_catalog.size() == 9, "language catalog changed size");
    std::set<std::wstring_view> codes;
    std::size_t chinese_targets = 0;
    for (const auto& row : language_catalog) {
        check(!row.code.empty() && !row.display.empty() && !row.prompt.empty(),
              "language catalog entry is missing a code, label or prompt name");
        check(codes.insert(row.code).second, "language catalog codes are not unique");
        check(find_language(row.code) == &row,
              "language lookup did not return the catalog row itself");
        if (row.chinese_target) ++chinese_targets;
    }
    check(chinese_targets == chinese_language_choices.size(),
          "Chinese target list does not match the catalog flags");
    check(language_choices.size() == language_catalog.size(),
          "language choice list does not match the catalog");
    const LanguageRow* simplified = find_language(L"zh-Hans");
    check(simplified != nullptr && !simplified->chinese_target,
          "zh-Hans must stay selectable without being a Chinese target");
    check(find_language(L"xx") == nullptr,
          "language lookup invented an entry for an unknown code");
}

void check_target_selection() {
    const std::wstring chinese = L"这是一段中文";
    for (const auto& [code, label] : chinese_language_choices) {
        check(target_for(chinese, code) == std::wstring(code),
              "language catalog offered a Chinese target the translator drops");
        // The combo label and the translation name must not drift apart.
        check(target_display_name(code) == std::wstring(label),
              "Chinese target label drifted from the language catalog");
    }
    check(target_for(chinese, L"zh-Hans") == L"en",
          "core self-test allowed translating Chinese into Simplified Chinese");
    check(target_for(chinese, L"unsupported") == L"en",
          "core self-test fell back to something other than English");
    check(target_for(L"Plain English text", L"ja") == L"zh-Hans",
          "core self-test did not translate foreign text into Simplified Chinese");
    check(target_display_name(L"zh-Hans") == L"简体中文" &&
              target_prompt_name(L"zh-Hans") == L"Simplified Chinese",
          "core self-test lost the Simplified Chinese display names");
    check(target_display_name(L"xx") == L"xx" && target_prompt_name(L"xx") == L"xx",
          "core self-test did not echo an unknown language code");

    // The screenshot page picks one output language and needs a sensible
    // fallback for every source script.
    check(target_for_selection(chinese, L"zh-Hans") == L"en",
          "core self-test translated Chinese into Chinese");
    check(target_for_selection(chinese, L"zh-Hant") == L"en",
          "core self-test translated Chinese into Traditional Chinese");
    check(target_for_selection(chinese, L"ja") == L"ja",
          "core self-test ignored the selected language for Chinese text");
    check(target_for_selection(L"Plain English text", L"zh-Hans") == L"zh-Hans",
          "core self-test did not default English text to Simplified Chinese");
    check(target_for_selection(L"こんにちは世界", L"ja") == L"zh-Hans",
          "core self-test translated Japanese into Japanese");
    check(target_for_selection(L"こんにちは世界", L"en") == L"en",
          "core self-test ignored the selected language for Japanese text");
    check(target_for_selection(L"Plain English text", L"ja") == L"ja",
          "core self-test ignored the selected language for English text");
    check(target_for_selection(chinese, L"unsupported") == target_for(chinese, L"en"),
          "core self-test did not fall back for an unknown selection");
    check(chinese_target_for_selection(L"zh-Hans") == L"en" &&
              chinese_target_for_selection(L"ja") == L"ja",
          "core self-test computed the wrong block editor target");
}

void check_target_matching() {
    check(matches_target(L"Hello world", L"en"),
          "core self-test rejected English output");
    check(!matches_target(L"你好世界", L"en"),
          "core self-test accepted Chinese text as English output");
    check(matches_target(L"你好世界", L"zh-Hans"),
          "core self-test rejected Simplified Chinese output");
    check(matches_target(L"Привет, мир", L"ru"),
          "core self-test rejected Russian output");
    check(matches_target(L"", L"ja"),
          "core self-test rejected empty output");
}

void check_settings_tables() {
    check(settings_internal::providers.size() == 6,
          "settings provider table changed size");
    std::set<std::wstring_view> provider_ids;
    int key_free_providers = 0;
    for (const auto& provider : settings_internal::providers) {
        check(!provider.id.empty() && !provider.label.empty(),
              "settings provider entry is missing its id or label");
        check(provider_ids.insert(provider.id).second,
              "settings provider ids are not unique");
        if (provider.kind == settings_internal::ProviderInfo::Kind::free) {
            ++key_free_providers;
        }
    }
    check(key_free_providers == 4,
          "settings provider table lost a key-free provider");
    check(settings_internal::ai_protocols.size() == 3,
          "settings large-model protocol table changed size");

    // The sidebar is ordered by page, so the table must stay a permutation of
    // the five pages or navigation_index_for_page silently returns page 0.
    auto pages = settings_internal::navigation_page_order;
    std::sort(pages.begin(), pages.end());
    check(pages == std::array<int, 5>{0, 1, 2, 3, 4},
          "settings navigation order is not a permutation of the pages");
    check(settings_internal::navigation_glyphs.size() ==
              settings_internal::navigation_page_order.size(),
          "settings navigation glyph table changed size");

    check(settings_internal::px(10, 96) == 10 &&
              settings_internal::px(10, 144) == 15 &&
              settings_internal::px(10, 192) == 20,
          "settings DPI scaling changed");
}

void check_provider_selection() {
    using settings_provider::id_at;
    using settings_provider::index_of;

    // The combo lists the catalog order without the large-model row; load and
    // save both translate a combo index through this mapping.
    constexpr std::array<std::wstring_view, 5> expected{
        L"tencent_free", L"microsoft_free", L"iciba_free", L"yandex_free", L"deepl",
    };
    check(settings_provider::traditional_count() == expected.size(),
          "provider combo row count changed");
    for (std::size_t index = 0; index < expected.size(); ++index) {
        check(id_at(index) == expected[index],
              "provider combo order changed");
        check(index_of(expected[index]) == index,
              "provider index mapping is not the inverse of the row order");
    }
    check(index_of(L"ai") == settings_provider::not_listed,
          "the large-model entry leaked into the provider combo");
    check(id_at(expected.size()) == expected.front(),
          "an empty provider combo no longer falls back to the first row");
    check(settings_provider::default_id() == L"microsoft_free",
          "the default provider changed");

    const auto free = settings_provider::fields_for(L"microsoft_free");
    check(free.provider_combo && !free.key && !free.profile_picker &&
              !free.protocol && !free.endpoint && !free.model && !free.model_refresh,
          "a key-free provider exposes paid-endpoint fields");
    const auto deepl = settings_provider::fields_for(L"deepl");
    check(deepl.provider_combo && deepl.key && !deepl.model,
          "DeepL lost its key field or gained model fields");
    const auto ai = settings_provider::fields_for(L"ai");
    check(!ai.provider_combo && ai.profile_picker && ai.protocol && ai.key &&
              ai.endpoint && ai.model && ai.model_refresh,
          "the large-model entry is missing part of its form");
}

void check_profile_state() {
    using settings_profiles::Profile;
    const std::vector<Profile> list{{L"A", L"openai_chat"}, {L"接口 2", L"openai_chat"}};

    // The combo must never end up without a row while the list is non-empty.
    check(settings_profiles::resolve_selection(list, L"接口 2") == L"接口 2",
          "profile selection lost an existing name");
    check(settings_profiles::resolve_selection(list, L"gone") == L"A",
          "profile selection does not fall back to the first row");
    check(settings_profiles::resolve_selection({}, L"A").empty(),
          "empty profile list produced a selection");

    check(settings_profiles::next_default_name({}) == L"接口 1",
          "first default profile name changed");
    check(settings_profiles::next_default_name(list) == L"接口 3",
          "default profile name does not count existing rows");
    check(settings_profiles::next_default_name({{L"接口 3"}}) == L"接口 2",
          "default profile name skipped a free row");

    // Removing must keep the row that slid into the erased position selected.
    auto after_first = list;
    after_first.erase(after_first.begin());
    check(settings_profiles::name_after_removal(after_first, 0) == L"接口 2",
          "profile removal selected the wrong neighbour");
    auto after_last = list;
    after_last.erase(after_last.begin() + 1);
    check(settings_profiles::name_after_removal(after_last, 1) == L"A",
          "removing the last profile did not fall back to the previous row");
    check(settings_profiles::name_after_removal({}, 0).empty(),
          "profile removal on an empty list produced a name");

    check(settings_profiles::unique_rename(list, 0, L"B") == L"B",
          "a free profile name was changed");
    check(settings_profiles::unique_rename(list, 0, L"接口 2") == L"接口 2 2",
          "a colliding profile name was not made unique");
    check(settings_profiles::unique_rename({{L"B"}, {L"A"}, {L"A 2"}}, 0, L"A") ==
              L"A 3",
          "profile rename did not skip taken suffixes");
    check(settings_profiles::unique_rename(list, 0, L"").empty(),
          "an empty profile rename produced a name");

    check(settings_profiles::clamp_index(-1, 2) == 0,
          "empty combo index no longer clamps to the first row");
    check(settings_profiles::clamp_index(9, 2) == 1,
          "stale combo index no longer clamps to the last row");
    check(settings_profiles::clamp_index(0, 0) == 0,
          "clamping against an empty list changed");
}

void check_ocr_options() {
    const auto placeholder = settings_ocr::build_rows({});
    check(placeholder.size() == 1 &&
              placeholder.front().label == L"未检测到 Windows OCR 语言包",
          "OCR options lost the no-language-pack placeholder");
    check(placeholder.front().languages ==
              std::vector<std::wstring>{L"zh-Hans-CN", L"en-US"},
          "the placeholder no longer keeps the default language set");

    const std::vector<std::wstring> installed{L"zh-Hans-CN", L"en-US"};
    const auto rows = settings_ocr::build_rows(installed);
    check(rows.size() == installed.size() + 1,
          "OCR options do not have one row per installed engine");
    check(rows.front().languages == installed &&
              rows.front().label == L"自动（推荐）",
          "the automatic OCR row no longer stores the installed set");
    check(rows[1].languages == std::vector<std::wstring>{L"zh-Hans-CN"} &&
              rows[1].label == L"只用简体中文",
          "a single-language OCR row is wrong");
    check(rows[2].label == L"只用英语",
          "an OCR language label came from the wrong catalog row");

    check(settings_ocr::find_row(rows, installed) == 0,
          "the saved set no longer matches the automatic row");
    check(settings_ocr::find_row(rows, {L"en-US", L"zh-Hans-CN"}) ==
              settings_ocr::not_found,
          "OCR row matching ignored the order of the saved set");
    check(settings_ocr::find_row(rows, {L"fr-FR"}) == settings_ocr::not_found,
          "a removed language pack still matches a row");

    const auto saved = settings_ocr::saved_row({L"zh-Hans-CN", L"nl-NL"});
    check(saved.label == L"当前设置（简体中文、nl-NL）",
          "the saved OCR row does not fall back to unknown tags");
    check(saved.languages == std::vector<std::wstring>{L"zh-Hans-CN", L"nl-NL"},
          "the saved OCR row changed the stored set");

    check(settings_ocr::installed_summary({}) == L"系统已安装：无",
          "the empty install summary changed");
    check(settings_ocr::installed_summary(installed) ==
              L"系统已安装：简体中文、英语",
          "the install summary does not name installed engines");

    check(settings_ocr::clamp_selection(-1, 3) == 0 &&
              settings_ocr::clamp_selection(9, 3) == 2 &&
              settings_ocr::clamp_selection(0, 0) == 0,
          "OCR combo index clamping changed");
}

void check_http_retry() {
    const HttpError timeout("timeout", 0, ERROR_WINHTTP_TIMEOUT);
    const HttpError refused("refused", 0, ERROR_WINHTTP_CANNOT_CONNECT);
    const HttpError unresolved("unresolved", 0, ERROR_WINHTTP_NAME_NOT_RESOLVED);
    const HttpError dropped("dropped", 0, ERROR_WINHTTP_CONNECTION_ERROR);
    const HttpError certificate("certificate", 0, ERROR_WINHTTP_SECURE_FAILURE);
    const HttpError status("bad status", 500);
    check(timeout.retryable() && refused.retryable() && unresolved.retryable() &&
              dropped.retryable(),
          "the HTTP retry policy stopped covering transient network failures");
    check(!certificate.retryable() && !status.retryable() &&
              !HttpError("plain").retryable(),
          "the HTTP retry policy would repeat a request that already reached the endpoint");
    check(timeout.status() == 0 && status.status() == 500,
          "the HTTP error status changed");
    const std::string described = describe_win32_error("probe", ERROR_WINHTTP_TIMEOUT);
    check(described.rfind("probe: ", 0) == 0 && described.size() > 8,
          "describe_win32_error no longer prefixes the operation");
}

void check_hotkeys() {
    using settings_internal::same_hotkey;
    std::wstring error;
    const auto capture = parse_hotkey(L"Ctrl+Alt+Q", &error);
    const auto toggle = parse_hotkey(L"Ctrl+Alt+W", &error);
    const auto text = parse_hotkey(L"Ctrl+Alt+Space", &error);
    const auto equivalent = parse_hotkey(L"control+alt+space", &error);
    const auto input_method = parse_hotkey(L"Ctrl+Space", &error);
    check(capture && toggle && text && equivalent && input_method,
          "core self-test could not parse the default hotkeys");
    check(!same_hotkey(*capture, *toggle) && !same_hotkey(*capture, *text) &&
              !same_hotkey(*toggle, *text),
          "core self-test treated distinct hotkeys as duplicates");
    check(same_hotkey(*text, *equivalent),
          "core self-test did not normalize Control to Ctrl");
    check(!parse_hotkey(L""),
          "core self-test accepted an empty hotkey");
    check(!parse_hotkey(L"Hyper+Q"),
          "core self-test accepted an unknown modifier");
    check(!parse_hotkey(L"Ctrl+"),
          "core self-test accepted a modifier without a key");
    const auto f13 = parse_hotkey(L"F13");
    check(f13 && f13->virtual_key == VK_F13 &&
              (f13->modifiers & MOD_NOREPEAT) != 0,
          "core self-test could not parse a function-key hotkey");
    const auto escape = parse_hotkey(L"ctrl+escape");
    check(escape && escape->virtual_key == VK_ESCAPE &&
              (escape->modifiers & MOD_CONTROL) != 0,
          "core self-test could not parse the Escape alias");
}

}  // namespace

void core_self_test() {
    check_language_catalog();
    check_language_detection();
    check_target_selection();
    check_target_matching();
    check_settings_tables();
    check_provider_selection();
    check_profile_state();
    check_ocr_options();
    check_http_retry();
    check_hotkeys();
}

}  // namespace minitrans
