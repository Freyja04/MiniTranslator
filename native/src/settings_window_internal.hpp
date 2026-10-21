#pragma once

#include "hotkey.hpp"
#include "language.hpp"
#include "settings_window.hpp"
#include "theme.hpp"
#include "ui_draw.hpp"
#include "ui_icons.hpp"
#include "util.hpp"

#include <commctrl.h>
#include <d2d1.h>
#include <shellapi.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Media.Ocr.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace minitrans {

using winrt::Windows::Data::Json::JsonArray;
using winrt::Windows::Data::Json::JsonObject;
using winrt::Windows::Data::Json::JsonValue;
using winrt::Windows::Data::Json::JsonValueType;
using ui_draw::draw_text;
using ui_draw::set_font;
using ui_draw::text_of;

namespace settings_internal {

constexpr wchar_t class_name[] = L"MiniTranslator.Native.SettingsWindow.v1";
constexpr int id_nav_first = 7001;
constexpr int id_nav_last = id_nav_first + 5;
constexpr int id_provider = 7010;
constexpr int id_test = 7013;
constexpr int id_open_config = 7014;
constexpr int id_restart = 7015;
constexpr int id_theme_mode = 7016;
constexpr int id_refresh_models = 7017;
constexpr int id_text_target = 7018;
constexpr int id_text_input = 7019;
constexpr int id_text_output = 7020;
constexpr int id_text_translate = 7021;
constexpr int id_text_clear = 7022;
constexpr int id_text_copy = 7023;
constexpr int id_translator_class = 7024;
constexpr int id_ai_protocol = 7026;
constexpr int id_profile_add = 7027;
constexpr int id_profile_remove = 7028;
constexpr int id_check_updates = 7029;
constexpr int id_copy_diagnostic = 7030;
constexpr UINT_PTR combo_subclass_id = 1;
constexpr UINT_PTR hotkey_subclass_id = 2;
constexpr UINT_PTR edit_subclass_id = 3;
constexpr UINT_PTR combo_edit_subclass_id = 4;

using theme::color_bad;
using theme::color_card;
using theme::color_input;
using theme::color_line;
using theme::color_line_high;
using theme::color_nav_active;
using theme::color_nav_hover;
using theme::color_ok;
using theme::color_page;
using theme::color_sidebar;
using theme::color_text;
using theme::color_text_dim;
using theme::color_text_faint;

constexpr int logical_sidebar_width = 166;
constexpr int logical_page_margin = 28;
constexpr int logical_content_top = 90;
constexpr int logical_control_height = 34;
constexpr int logical_card_padding = 12;
constexpr int logical_row_gap = 12;
constexpr int logical_hint_gap = 6;
constexpr int logical_hint_height = 20;
constexpr int logical_panel_header_height = 30;
constexpr int logical_section_gap = 16;
constexpr int logical_section_height = 20;
constexpr int logical_section_to_card = 8;
constexpr int logical_panel_gap = 14;
constexpr int logical_field_min_offset = 56;
constexpr int logical_field_max_offset = 180;
constexpr int logical_label_gap = 10;
constexpr int logical_setting_row_height = logical_control_height + logical_row_gap;
constexpr int logical_edit_inset_x = 3;
constexpr int logical_edit_inset_top = 8;
constexpr int logical_edit_inset_bottom = 4;
constexpr int logical_edit_corner_radius = 8;
constexpr int logical_field_text_offset_y = 0;
constexpr int logical_combo_edit_offset_y = 4;

constexpr int logical_translation_toolbar_bottom =
    logical_content_top + logical_control_height + logical_card_padding * 2;
constexpr int logical_translation_input_panel_top =
    logical_translation_toolbar_bottom + logical_panel_gap;
constexpr int logical_translation_input_panel_bottom =
    logical_translation_input_panel_top + logical_panel_header_height + 118 +
    logical_card_padding;
constexpr int logical_translation_output_panel_top =
    logical_translation_input_panel_bottom + logical_panel_gap;
constexpr int logical_translation_output_panel_bottom =
    logical_translation_output_panel_top + logical_panel_header_height + 150 +
    logical_card_padding;
constexpr int logical_ocr_engine_top =
    logical_content_top + logical_card_padding;
constexpr int logical_ocr_engine_note_top =
    logical_ocr_engine_top + logical_control_height + logical_hint_gap;
constexpr int logical_ocr_language_top =
    logical_ocr_engine_note_top + logical_hint_height + logical_row_gap;
constexpr int logical_ocr_upscale_top =
    logical_ocr_language_top + logical_setting_row_height;
constexpr int logical_ocr_main_bottom =
    logical_ocr_upscale_top + logical_control_height + logical_card_padding;
constexpr int logical_ocr_language_section_top =
    logical_ocr_main_bottom + logical_section_gap;
constexpr int logical_ocr_language_card_top =
    logical_ocr_language_section_top + logical_section_height +
    logical_section_to_card;
// Text-only OCR cards need a little more breathing room than control cards.
constexpr int logical_ocr_text_card_padding = 16;
constexpr int logical_ocr_installed_note_top =
    logical_ocr_language_card_top + logical_ocr_text_card_padding;
constexpr int logical_ocr_language_note_top =
    logical_ocr_installed_note_top + logical_hint_height + logical_hint_gap;
constexpr int logical_ocr_language_note_height = 32;
constexpr int logical_ocr_language_card_bottom =
    logical_ocr_language_note_top + logical_ocr_language_note_height +
    logical_ocr_text_card_padding;
constexpr int logical_window_height = 650;
constexpr int logical_status_height = 20;
constexpr int logical_status_margin = 8;
constexpr int logical_status_bottom_margin = 11;
constexpr int logical_status_top =
    logical_window_height - logical_status_bottom_margin - logical_status_height;
constexpr int logical_content_limit =
    logical_status_top - logical_status_margin;
static_assert(logical_ocr_language_card_bottom <= logical_content_limit);

constexpr std::array<std::wstring_view, 6> page_names{
    L"快捷键", L"翻译接口", L"截图识别", L"显示", L"其他", L"文本翻译",
};
constexpr std::array<std::wstring_view, 6> page_subtitles{
    L"点一下输入框，然后直接按下想要的组合键。",
    L"识别到的文字送到哪个接口去翻。改完记得点一下「测试连接」。",
    L"选择截图里的文字用什么方式读出来。",
    L"主题和复制行为。",
    L"开机自启、配置文件和故障排查。",
    L"输入或粘贴文字，停止输入 500 毫秒后自动翻译。",
};

// Keep the existing settings page indices stable while presenting the two
// translation entry points first, followed by configuration pages.
constexpr std::array<int, 6> navigation_page_order{5, 2, 1, 0, 3, 4};

// These code points are shared by Segoe Fluent Icons and Segoe MDL2 Assets.
// Indexed by settings page rather than sidebar position.
constexpr std::array<wchar_t, 6> navigation_glyphs{
    0xE765,  // Keyboard -> 快捷键
    0xE8AB,  // Switch   -> 翻译接口
    ui_icons::capture_translation,  // Crop -> 截图识别
    0xE7F4,  // Monitor  -> 显示
    0xE713,  // Settings -> 其他
    ui_icons::text_translation,  // Edit -> 文本翻译
};


constexpr std::array<std::wstring_view, 3> theme_modes{
    L"跟随系统", L"浅色", L"深色",
};

namespace hotkey_index {
constexpr std::size_t text_label = 0;
constexpr std::size_t text_edit = 1;
constexpr std::size_t capture_label = 2;
constexpr std::size_t capture_edit = 3;
constexpr std::size_t toggle_label = 4;
constexpr std::size_t toggle_edit = 5;
constexpr std::size_t selection_help = 6;
}  // namespace hotkey_index

namespace translation_index {
constexpr std::size_t class_label = 0;
constexpr std::size_t class_combo = 1;
constexpr std::size_t provider_label = 2;
constexpr std::size_t provider_combo = 3;
constexpr std::size_t profile_label = 4;
constexpr std::size_t profile_combo = 5;
constexpr std::size_t profile_add = 6;
constexpr std::size_t profile_remove = 7;
constexpr std::size_t protocol_label = 8;
constexpr std::size_t protocol_combo = 9;
constexpr std::size_t key_label = 10;
constexpr std::size_t key_edit = 11;
constexpr std::size_t endpoint_label = 12;
constexpr std::size_t endpoint_edit = 13;
constexpr std::size_t model_label = 14;
constexpr std::size_t model_combo = 15;
constexpr std::size_t refresh_button = 16;
constexpr std::size_t test_button = 17;
constexpr std::size_t test_status = 18;
constexpr std::size_t provider_hint = 19;
constexpr std::size_t target_label = 20;
constexpr std::size_t target_combo = 21;
}  // namespace translation_index

namespace ocr_index {
constexpr std::size_t engine_label = 0;
constexpr std::size_t engine_value = 1;
constexpr std::size_t engine_note = 2;
constexpr std::size_t language_label = 3;
constexpr std::size_t language_combo = 4;
constexpr std::size_t installed_label = 5;
constexpr std::size_t upscale_checkbox = 6;
}  // namespace ocr_index

namespace appearance_index {
constexpr std::size_t theme_label = 0;
constexpr std::size_t theme_combo = 1;
constexpr std::size_t auto_copy_checkbox = 2;
constexpr std::size_t copy_label = 3;
}  // namespace appearance_index

namespace other_index {
constexpr std::size_t autostart_checkbox = 0;
constexpr std::size_t config_label = 1;
constexpr std::size_t config_path = 2;
constexpr std::size_t open_config = 3;
constexpr std::size_t restart = 4;
constexpr std::size_t check_updates = 5;
constexpr std::size_t config_hint = 6;
constexpr std::size_t diagnostic_command = 7;
constexpr std::size_t copy_diagnostic = 8;
}  // namespace other_index


struct ProviderInfo {
    std::wstring_view id;
    std::wstring_view label;
    enum class Kind { free, deepl, ai } kind;
};

constexpr std::array<ProviderInfo, 6> providers{{
    {L"tencent_free", L"腾讯交互翻译（免密钥）", ProviderInfo::Kind::free},
    {L"microsoft_free", L"微软翻译（免密钥）", ProviderInfo::Kind::free},
    {L"iciba_free", L"词霸翻译（免密钥）", ProviderInfo::Kind::free},
    {L"yandex_free", L"Yandex 翻译（免密钥）", ProviderInfo::Kind::free},
    {L"deepl", L"DeepL", ProviderInfo::Kind::deepl},
    {L"ai", L"大模型翻译", ProviderInfo::Kind::ai},
}};

// The three wire formats a user-defined large-model endpoint can speak.
constexpr std::array<std::pair<std::wstring_view, std::wstring_view>, 3> ai_protocols{{
    {L"openai_chat", L"OpenAI Chat Completions"},
    {L"openai_responses", L"OpenAI Responses"},
    {L"anthropic_messages", L"Anthropic Messages"},
}};

constexpr std::array<std::pair<std::wstring_view, std::wstring_view>, 8> zh_targets{{
    {L"en", L"英语"}, {L"ja", L"日语"}, {L"ko", L"韩语"},
    {L"fr", L"法语"}, {L"de", L"德语"}, {L"es", L"西班牙语"},
    {L"ru", L"俄语"}, {L"zh-Hant", L"繁体中文"},
}};


inline std::size_t navigation_index_for_page(int page) noexcept {
    const auto found = std::find(navigation_page_order.begin(),
                                 navigation_page_order.end(), page);
    return found == navigation_page_order.end()
        ? 0U : static_cast<std::size_t>(std::distance(navigation_page_order.begin(), found));
}

class ScopedRedrawPause {
public:
    explicit ScopedRedrawPause(HWND window) noexcept
        : window_(window), active_(window && IsWindowVisible(window)) {
        if (active_) SendMessageW(window_, WM_SETREDRAW, FALSE, 0);
    }

    ~ScopedRedrawPause() {
        if (active_ && IsWindow(window_)) {
            SendMessageW(window_, WM_SETREDRAW, TRUE, 0);
            RedrawWindow(window_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_FRAME |
                             RDW_UPDATENOW | RDW_NOERASE);
        }
    }

    ScopedRedrawPause(const ScopedRedrawPause&) = delete;
    ScopedRedrawPause& operator=(const ScopedRedrawPause&) = delete;

private:
    HWND window_{};
    bool active_{};
};


inline int px(int value, int dpi) noexcept {
    return MulDiv(value, dpi, 96);
}

}  // namespace settings_internal

namespace settings_internal {

void align_combo_edit(HWND combo, int dpi) noexcept;
void set_rounded_control_region(HWND control, int radius) noexcept;
void fill_round_rect(HDC dc, const RECT& rect, int radius,
                     COLORREF fill, COLORREF border);
void draw_settings_nav_icon(HDC dc, int page, const RECT& bounds,
                            COLORREF color, HFONT icon_font);
HFONT create_icon_font(int dpi);
void clear_combo_edit_selection(HWND combo) noexcept;
void set_combo_edit_redraw(HWND combo, bool enabled) noexcept;
COLORREF blend_color(COLORREF base, COLORREF tint, int tint_percent) noexcept;
void style_combo_popup(HWND combo, int dpi, HFONT font) noexcept;
int selected_index(HWND combo);
void select_combo(HWND combo, int index);
void populate_text_targets(HWND combo);
int text_target_index(std::wstring_view target);
std::wstring text_target_at(HWND combo);
std::wstring text_target_selector(const TextTranslationSession& session);
std::wstring text_translation_status(const TextTranslationSession& session);
bool is_modifier_key(UINT key) noexcept;
std::wstring hotkey_key_name(UINT key);
bool autostart_enabled();
void set_autostart(bool enabled);
std::wstring ocr_language_name(std::wstring_view tag);
bool same_hotkey(const HotkeySpec& left, const HotkeySpec& right) noexcept;

}  // namespace settings_internal


}  // namespace minitrans
