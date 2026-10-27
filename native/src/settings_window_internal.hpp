#pragma once

#include "hotkey.hpp"
#include "language.hpp"
#include "language_catalog.hpp"
#include "settings_catalog.hpp"
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
#include <span>
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
constexpr int id_nav_last = id_nav_first + 4;
constexpr int id_provider = 7010;
constexpr int id_test = 7013;
constexpr int id_diagnostics = 7015;
// The segmented 主题 row: three consecutive ids after the page's other
// controls, clear of the 7018+ text-translation range below.
constexpr int id_theme_mode = 7140;
constexpr int id_theme_light = 7141;
constexpr int id_theme_dark = 7142;
constexpr int id_text_target = 7018;
constexpr int id_text_input = 7019;
constexpr int id_text_output = 7020;
constexpr int id_text_clear = 7022;
constexpr int id_text_copy = 7023;
constexpr int id_text_source = 7024;
constexpr int id_text_swap = 7025;
constexpr int id_class_traditional = 7026;
constexpr int id_class_ai = 7027;
constexpr int id_check_updates = 7029;
constexpr int id_text_capture = 7030;
constexpr int id_ai_list = 7031;
constexpr int id_ai_add = 7032;
constexpr UINT_PTR combo_subclass_id = 1;
constexpr UINT_PTR hotkey_subclass_id = 2;
constexpr UINT_PTR edit_subclass_id = 3;
constexpr UINT_PTR combo_edit_subclass_id = 4;
constexpr UINT_PTR list_subclass_id = 5;

using theme::color_bad;
using theme::color_card;
using theme::color_input;
using theme::color_hover;
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
// Pages have no title or subtitle, so their content starts right below the
// window edge instead of leaving an empty band where the header used to be.
constexpr int logical_content_top = 20;
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

// The 类别 switch is two owner-drawn buttons that read as one segmented
// control, so both halves share the field column and only the active half is
// filled. The endpoint list is a card capped at a few rows so the page never
// pushes the hint under the footer. The row height and the row inset come from
// settings_ai_list, which paints and hit tests with them, so they cannot drift
// apart here.
constexpr int logical_class_corner_radius = 8;
constexpr int logical_ai_list_max_rows = 5;
constexpr int logical_ai_add_width = 64;
constexpr int logical_provider_gap = 8;

constexpr int logical_ocr_engine_top =
    logical_content_top + logical_card_padding;
constexpr int logical_ocr_language_top =
    logical_ocr_engine_top + logical_setting_row_height;
// The output language lives next to the recognition language: it is the one
// choice that decides what the recognized text is translated into.
constexpr int logical_ocr_target_top =
    logical_ocr_language_top + logical_setting_row_height;
constexpr int logical_ocr_upscale_top =
    logical_ocr_target_top + logical_setting_row_height;
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

// The text translation page mirrors the quick translation window: the capture
// button and the language row share the page margin, one card holds both
// editors, and the auto-copy option closes the page below the status row.
constexpr int logical_translation_toolbar_top = logical_content_top;
constexpr int logical_translation_body_top =
    logical_content_top + logical_control_height + 14;
constexpr int logical_translation_auto_copy_top =
    logical_content_limit - logical_card_padding - logical_control_height;
constexpr int logical_translation_footer_top =
    logical_translation_auto_copy_top - logical_hint_gap - logical_hint_height;
constexpr int logical_translation_body_bottom =
    logical_translation_footer_top - 10;
constexpr int logical_translation_divider =
    logical_translation_body_top +
    (logical_translation_body_bottom - logical_translation_body_top) / 2;
constexpr int logical_translation_label_height = 29;
constexpr int logical_translation_inset = 13;
static_assert(logical_translation_divider > logical_translation_body_top);
static_assert(logical_translation_body_bottom - 1 >
              logical_translation_divider + logical_translation_label_height);
static_assert(logical_translation_footer_top > logical_translation_body_bottom);
static_assert(logical_translation_auto_copy_top >
              logical_translation_footer_top + logical_hint_height);
static_assert(logical_translation_auto_copy_top + logical_control_height <=
              logical_content_limit);

constexpr std::array<std::wstring_view, 5> page_names{
    L"快捷键", L"翻译接口", L"截图识别", L"设置", L"翻译",
};

// Keep the existing settings page indices stable while presenting the two
// translation entry points first, followed by configuration pages.
constexpr std::array<int, 5> navigation_page_order{4, 2, 1, 0, 3};

// These code points are shared by Segoe Fluent Icons and Segoe MDL2 Assets.
// Indexed by settings page rather than sidebar position.
constexpr std::array<wchar_t, 5> navigation_glyphs{
    0xE765,  // Keyboard -> 快捷键
    0xE8AB,  // Switch   -> 翻译接口
    ui_icons::capture_translation,  // Crop -> 截图识别
    0xE713,  // Settings -> 设置
    ui_icons::text_translation,  // Edit -> 翻译
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
}  // namespace hotkey_index

namespace translation_index {
// Page 1 control order, which is exactly the order create_translation_page
// appends to page_controls_[1].
constexpr std::size_t class_label = 0;
constexpr std::size_t class_traditional = 1;
constexpr std::size_t class_ai = 2;
constexpr std::size_t provider_label = 3;
constexpr std::size_t provider_combo = 4;
constexpr std::size_t ai_caption = 5;
constexpr std::size_t ai_list = 6;
constexpr std::size_t key_label = 7;
constexpr std::size_t key_edit = 8;
constexpr std::size_t test_button = 9;
constexpr std::size_t test_status = 10;
constexpr std::size_t ai_add = 11;
constexpr std::size_t provider_hint = 12;
constexpr std::size_t control_count = 13;
}  // namespace translation_index

namespace ocr_index {
constexpr std::size_t engine_label = 0;
constexpr std::size_t engine_value = 1;
constexpr std::size_t language_label = 2;
constexpr std::size_t language_combo = 3;
constexpr std::size_t target_label = 4;
constexpr std::size_t target_combo = 5;
constexpr std::size_t installed_label = 6;
constexpr std::size_t upscale_checkbox = 7;
}  // namespace ocr_index

namespace other_index {
// Creation order in create_other_page(): the caption, the three theme chips,
// the startup toggle, the two maintenance buttons, the inline update status
// and finally the version line.
constexpr std::size_t theme_label = 0;
constexpr std::size_t theme_chip_first = 1;
constexpr std::size_t autostart = 4;
constexpr std::size_t diagnostics = 5;
constexpr std::size_t check_updates = 6;
constexpr std::size_t update_status = 7;
constexpr std::size_t version_label = 8;
}  // namespace other_index

namespace text_translation_index {
constexpr std::size_t capture = 0;
constexpr std::size_t source_combo = 1;
constexpr std::size_t swap = 2;
constexpr std::size_t target_combo = 3;
constexpr std::size_t clear = 4;
constexpr std::size_t copy = 5;
constexpr std::size_t input = 6;
constexpr std::size_t output = 7;
constexpr std::size_t auto_copy = 8;
}  // namespace text_translation_index

// Provider and protocol tables live in settings_catalog.hpp so the settings
// state layer can be tested without pulling in the window types.

// The Chinese-target drop-down is derived from language_catalog: only the rows
// that the translator accepts as a target for Chinese source text.

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
// One chip of a segmented option row: the active segment reads as a raised
// field, the rest as plain captions, so the row reads like image-2 style
// segmented controls rather than a drop-down.
void draw_segment_option(HDC dc, const RECT& bounds, std::wstring_view caption,
                         HFONT font, int dpi, bool active, bool hot, bool focused,
                         HBRUSH surface);
// Natural chip widths for one segmented row: caption width plus the shared
// horizontal padding, in device pixels, in caption order.
[[nodiscard]] std::vector<int> segment_widths(HDC dc, HFONT font,
                                              std::span<const std::wstring_view> captions,
                                              int dpi);
void draw_settings_nav_icon(HDC dc, int page, const RECT& bounds,
                            COLORREF color, HFONT icon_font);
HFONT create_icon_font(int dpi, int logical_pixels = 17);
void clear_combo_edit_selection(HWND combo) noexcept;
void set_combo_edit_redraw(HWND combo, bool enabled) noexcept;
COLORREF blend_color(COLORREF base, COLORREF tint, int tint_percent) noexcept;
void style_combo_popup(HWND combo, int dpi, HFONT font) noexcept;
int selected_index(HWND combo);
void select_combo(HWND combo, int index);
void populate_text_targets(HWND combo);
void populate_text_sources(HWND combo);
int text_target_index(std::wstring_view target);
int text_source_index(std::wstring_view source);
std::wstring text_target_at(HWND combo);
std::wstring text_source_at(HWND combo);
std::wstring text_target_selector(const TextTranslationSession& session);
std::wstring text_translation_status(const TextTranslationSession& session);
bool is_modifier_key(UINT key) noexcept;
std::wstring hotkey_key_name(UINT key);
bool autostart_enabled();
void set_autostart(bool enabled);
bool same_hotkey(const HotkeySpec& left, const HotkeySpec& right) noexcept;

}  // namespace settings_internal


}  // namespace minitrans
