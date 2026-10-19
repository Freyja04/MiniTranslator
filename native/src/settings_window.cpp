#include "settings_window.hpp"

#include "hotkey.hpp"
#include "language.hpp"
#include "resource.h"
#include "translator.hpp"
#include "util.hpp"

#include <commctrl.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Media.Ocr.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <set>

namespace screentrans {

using winrt::Windows::Data::Json::JsonArray;
using winrt::Windows::Data::Json::JsonObject;
using winrt::Windows::Data::Json::JsonValue;
using winrt::Windows::Data::Json::JsonValueType;

namespace {

constexpr wchar_t class_name[] = L"ScreenTranslate.Native.SettingsWindow.v1";
constexpr int id_nav_first = 7001;
constexpr int id_nav_last = id_nav_first + 5;
constexpr int id_provider = 7010;
constexpr int id_test = 7013;
constexpr int id_open_config = 7014;
constexpr int id_restart = 7015;
constexpr int id_accent = 7016;
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
constexpr UINT_PTR combo_subclass_id = 1;
constexpr UINT_PTR hotkey_subclass_id = 2;
constexpr UINT_PTR edit_subclass_id = 3;
constexpr UINT_PTR combo_edit_subclass_id = 4;

constexpr COLORREF color_sidebar = RGB(16, 17, 22);
constexpr COLORREF color_page = RGB(21, 22, 27);
constexpr COLORREF color_card = RGB(27, 29, 35);
constexpr COLORREF color_input = RGB(33, 36, 41);
constexpr COLORREF color_line = RGB(40, 43, 51);
constexpr COLORREF color_line_high = RGB(52, 56, 66);
constexpr COLORREF color_text = RGB(231, 233, 236);
constexpr COLORREF color_text_dim = RGB(148, 154, 164);
constexpr COLORREF color_text_faint = RGB(110, 116, 126);
constexpr COLORREF color_bad = RGB(255, 107, 107);
constexpr COLORREF color_ok = RGB(78, 209, 139);

constexpr int logical_sidebar_width = 166;
constexpr int logical_page_margin = 28;
constexpr int logical_content_top = 90;
constexpr int logical_field_min_offset = 56;
constexpr int logical_field_max_offset = 180;
constexpr int logical_label_gap = 10;
constexpr int logical_setting_row_height = 43;
constexpr int logical_control_height = 34;
constexpr int logical_edit_inset_x = 3;
constexpr int logical_edit_inset_top = 8;
constexpr int logical_edit_inset_bottom = 4;
constexpr int logical_edit_corner_radius = 8;
constexpr int logical_field_text_offset_y = 0;
constexpr int logical_combo_edit_offset_y = 4;

constexpr std::array<std::wstring_view, 6> page_names{
    L"快捷键", L"翻译接口", L"截图识别", L"显示", L"其他", L"文本翻译",
};
constexpr std::array<std::wstring_view, 6> page_subtitles{
    L"点一下输入框，然后直接按下想要的组合键。",
    L"识别到的文字送到哪个接口去翻。改完记得点一下「测试连接」。",
    L"选择截图里的文字用什么方式读出来。",
    L"译文覆盖在原文上之后的样子和行为。",
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
    0xE7C3,  // Page     -> 截图识别
    0xE7F4,  // Monitor  -> 显示
    0xE713,  // Settings -> 其他
    0xE8C1,  // Message  -> 文本翻译
};

std::size_t navigation_index_for_page(int page) noexcept {
    const auto found = std::find(navigation_page_order.begin(),
                                 navigation_page_order.end(), page);
    return found == navigation_page_order.end()
        ? 0U : static_cast<std::size_t>(std::distance(navigation_page_order.begin(), found));
}

constexpr std::array<std::pair<std::wstring_view, COLORREF>, 8> accent_swatches{{
    {L"#28C76F", RGB(40, 199, 111)},
    {L"#4C8DFF", RGB(76, 141, 255)},
    {L"#8C7CF0", RGB(140, 124, 240)},
    {L"#FF7A45", RGB(255, 122, 69)},
    {L"#F45B7A", RGB(244, 91, 122)},
    {L"#F2C744", RGB(242, 199, 68)},
    {L"#00C2C7", RGB(0, 194, 199)},
    {L"#E8EAEE", RGB(232, 234, 238)},
}};

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
                         RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN |
                             RDW_FRAME | RDW_UPDATENOW);
        }
    }

    ScopedRedrawPause(const ScopedRedrawPause&) = delete;
    ScopedRedrawPause& operator=(const ScopedRedrawPause&) = delete;

private:
    HWND window_{};
    bool active_{};
};

int px(int value, int dpi) noexcept {
    return MulDiv(value, dpi, 96);
}

void align_combo_edit(HWND combo, int dpi) noexcept {
    if (!combo) return;
    COMBOBOXINFO info{sizeof(info)};
    if (!GetComboBoxInfo(combo, &info) || !info.hwndItem ||
        info.hwndItem == combo) {
        return;
    }

    RECT item = info.rcItem;
    RECT combo_screen{};
    GetWindowRect(combo, &combo_screen);
    const bool screen_coordinates =
        item.left >= combo_screen.left - 2 && item.top >= combo_screen.top - 2 &&
        item.right <= combo_screen.right + 2 && item.bottom <= combo_screen.bottom + 2;
    if (screen_coordinates) {
        MapWindowPoints(HWND_DESKTOP, combo,
                        reinterpret_cast<POINT*>(&item), 2);
    }

    const int width = item.right - item.left;
    const int height = item.bottom - item.top;
    const int offset = std::min(std::max(1, px(logical_combo_edit_offset_y, dpi)),
                                std::max(0, height - 1));
    if (width <= 0 || height <= offset) return;

    RECT current{};
    if (GetWindowRect(info.hwndItem, &current)) {
        MapWindowPoints(HWND_DESKTOP, combo,
                        reinterpret_cast<POINT*>(&current), 2);
        if (current.left == item.left && current.top == item.top + offset &&
            current.right == item.right && current.bottom == item.bottom) {
            return;
        }
    }
    SetWindowPos(info.hwndItem, nullptr, item.left, item.top + offset,
                 width, height - offset,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
}

void fill_round_rect(HDC dc, const RECT& rect, int radius,
                     COLORREF fill, COLORREF border) {
    static ID2D1Factory* factory = [] {
        ID2D1Factory* value = nullptr;
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &value);
        return value;
    }();
    ID2D1DCRenderTarget* target = nullptr;
    if (factory && rect.right > rect.left && rect.bottom > rect.top) {
        D2D1_RENDER_TARGET_PROPERTIES properties{};
        properties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
        properties.usage = D2D1_RENDER_TARGET_USAGE_NONE;
        properties.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
        if (SUCCEEDED(factory->CreateDCRenderTarget(&properties, &target)) &&
            SUCCEEDED(target->BindDC(dc, &rect))) {
            ID2D1SolidColorBrush* fill_brush = nullptr;
            ID2D1SolidColorBrush* border_brush = nullptr;
            target->CreateSolidColorBrush(
                D2D1::ColorF(GetRValue(fill) / 255.0F, GetGValue(fill) / 255.0F,
                             GetBValue(fill) / 255.0F),
                &fill_brush);
            target->CreateSolidColorBrush(
                D2D1::ColorF(GetRValue(border) / 255.0F, GetGValue(border) / 255.0F,
                             GetBValue(border) / 255.0F),
                &border_brush);
            target->BeginDraw();
            target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            const auto rounded = D2D1::RoundedRect(
                D2D1::RectF(0.5F, 0.5F,
                            static_cast<float>(rect.right - rect.left) - 0.5F,
                            static_cast<float>(rect.bottom - rect.top) - 0.5F),
                static_cast<float>(radius), static_cast<float>(radius));
            if (fill_brush) target->FillRoundedRectangle(rounded, fill_brush);
            if (border_brush) target->DrawRoundedRectangle(rounded, border_brush, 1.0F);
            const HRESULT drawn = target->EndDraw();
            if (fill_brush) fill_brush->Release();
            if (border_brush) border_brush->Release();
            target->Release();
            if (SUCCEEDED(drawn)) return;
            target = nullptr;
        }
    }
    if (target) target->Release();
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    if (!brush || !pen) {
        if (pen) DeleteObject(pen);
        if (brush) DeleteObject(brush);
        return;
    }
    const auto old_brush = SelectObject(dc, brush);
    const auto old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom,
              radius * 2, radius * 2);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void draw_text(HDC dc, std::wstring_view text, RECT rect, HFONT font,
               COLORREF color, UINT format) {
    const auto old_font = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &rect, format | DT_NOPREFIX);
    SelectObject(dc, old_font);
}

void draw_settings_nav_icon(HDC dc, int page, const RECT& bounds,
                            COLORREF color, HFONT icon_font) {
    if (page < 0 || page >= static_cast<int>(navigation_glyphs.size()) || !icon_font) return;
    const wchar_t glyph = navigation_glyphs[static_cast<std::size_t>(page)];
    draw_text(dc, std::wstring_view(&glyph, 1), bounds, icon_font, color,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

HFONT create_icon_font(int dpi) {
    constexpr std::array<const wchar_t*, 3> faces{
        L"Segoe Fluent Icons", L"Segoe MDL2 Assets", L"Segoe UI Symbol",
    };
    for (const auto* face : faces) {
        HFONT candidate = CreateFontW(
            -px(17, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
        if (!candidate) continue;
        HDC dc = GetDC(nullptr);
        wchar_t actual[LF_FACESIZE]{};
        if (dc) {
            const auto old_font = SelectObject(dc, candidate);
            GetTextFaceW(dc, static_cast<int>(std::size(actual)), actual);
            SelectObject(dc, old_font);
            ReleaseDC(nullptr, dc);
        }
        if (_wcsicmp(actual, face) == 0 || face == faces.back()) return candidate;
        DeleteObject(candidate);
    }
    return nullptr;
}

void clear_combo_edit_selection(HWND combo) noexcept {
    if (!combo) return;
    const int length = GetWindowTextLengthW(combo);
    SendMessageW(combo, CB_SETEDITSEL, 0, MAKELPARAM(length, length));
    COMBOBOXINFO info{sizeof(COMBOBOXINFO)};
    if (GetComboBoxInfo(combo, &info) && info.hwndItem) {
        const int edit_length = GetWindowTextLengthW(info.hwndItem);
        SendMessageW(info.hwndItem, EM_SETSEL, edit_length, edit_length);
    }
}

void set_combo_edit_redraw(HWND combo, bool enabled) noexcept {
    if (!combo) return;
    COMBOBOXINFO info{sizeof(info)};
    if (!GetComboBoxInfo(combo, &info) || !info.hwndItem ||
        info.hwndItem == combo) {
        return;
    }
    if (!enabled) {
        SendMessageW(info.hwndItem, WM_SETREDRAW, FALSE, 0);
        clear_combo_edit_selection(combo);
        return;
    }
    clear_combo_edit_selection(combo);
    SendMessageW(info.hwndItem, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(info.hwndItem, nullptr, FALSE);
}

COLORREF blend_color(COLORREF base, COLORREF tint, int tint_percent) noexcept {
    const int amount = std::clamp(tint_percent, 0, 100);
    const auto channel = [amount](BYTE base_value, BYTE tint_value) {
        return static_cast<BYTE>((base_value * (100 - amount) + tint_value * amount) / 100);
    };
    return RGB(channel(GetRValue(base), GetRValue(tint)),
               channel(GetGValue(base), GetGValue(tint)),
               channel(GetBValue(base), GetBValue(tint)));
}

void style_combo_popup(HWND combo, int dpi, HFONT font) noexcept {
    if (!combo) return;
    SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), px(30, dpi));
    SendMessageW(combo, CB_SETITEMHEIGHT, 0, px(30, dpi));
    SendMessageW(combo, CB_SETMINVISIBLE, 8, 0);

    RECT combo_rect{};
    GetWindowRect(combo, &combo_rect);
    int dropped_width = std::max(1, static_cast<int>(combo_rect.right - combo_rect.left));
    HDC dc = GetDC(combo);
    HGDIOBJ old_font = nullptr;
    if (dc && font) old_font = SelectObject(dc, font);
    const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int index = 0; dc && index < count; ++index) {
        const int length = static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, index, 0));
        if (length <= 0) continue;
        std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
        SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(value.data()));
        SIZE measured{};
        if (GetTextExtentPoint32W(dc, value.data(), length, &measured)) {
            dropped_width = std::max(
                dropped_width, static_cast<int>(measured.cx) + px(54, dpi));
        }
    }
    if (dc) {
        if (old_font) SelectObject(dc, old_font);
        ReleaseDC(combo, dc);
    }
    SendMessageW(combo, CB_SETDROPPEDWIDTH,
                 std::min(dropped_width, px(460, dpi)), 0);

    COMBOBOXINFO info{sizeof(info)};
    if (!GetComboBoxInfo(combo, &info) || !info.hwndList) return;
    SetWindowTheme(info.hwndList, L"DarkMode_Explorer", nullptr);
    constexpr DWORD immersive_dark_mode = 20;
    constexpr DWORD window_corner_preference = 33;
    constexpr int round_small = 3;
    BOOL dark = TRUE;
    DwmSetWindowAttribute(info.hwndList, immersive_dark_mode, &dark, sizeof(dark));
    DwmSetWindowAttribute(info.hwndList, window_corner_preference,
                          &round_small, sizeof(round_small));
    const LONG_PTR ex_style = GetWindowLongPtrW(info.hwndList, GWL_EXSTYLE);
    SetWindowLongPtrW(info.hwndList, GWL_EXSTYLE,
                      ex_style & ~static_cast<LONG_PTR>(WS_EX_CLIENTEDGE));
    SetWindowPos(info.hwndList, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_FRAMECHANGED);
}

void enable_dark_frame(HWND window) noexcept {
    constexpr DWORD immersive_dark_mode = 20;
    constexpr DWORD border_color = 34;
    constexpr DWORD caption_color = 35;
    constexpr DWORD caption_text_color = 36;
    BOOL enabled = TRUE;
    DwmSetWindowAttribute(window, immersive_dark_mode, &enabled, sizeof(enabled));
    const COLORREF border = color_line_high;
    const COLORREF caption = color_sidebar;
    const COLORREF text = color_text;
    DwmSetWindowAttribute(window, border_color, &border, sizeof(border));
    DwmSetWindowAttribute(window, caption_color, &caption, sizeof(caption));
    DwmSetWindowAttribute(window, caption_text_color, &text, sizeof(text));
}

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

std::wstring text_of(HWND control) {
    const int length = GetWindowTextLengthW(control);
    std::wstring value(static_cast<std::size_t>(std::max(0, length)) + 1, L'\0');
    if (length > 0) GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(std::max(0, length)));
    return value;
}

void set_font(HWND control, HFONT font) {
    if (control) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

int selected_index(HWND combo) {
    const LRESULT value = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return value == CB_ERR ? 0 : static_cast<int>(value);
}

void select_combo(HWND combo, int index) {
    SendMessageW(combo, CB_SETCURSEL, std::max(0, index), 0);
}

void select_combo_text(HWND combo, std::wstring_view value) {
    const LRESULT found = SendMessageW(
        combo, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1),
        reinterpret_cast<LPARAM>(std::wstring(value).c_str()));
    select_combo(combo, found == CB_ERR ? 0 : static_cast<int>(found));
}

void populate_text_targets(HWND combo) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"自动选择"));
    for (const auto target : text_translation_targets()) {
        const auto label = target_display_name(target);
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(label.c_str()));
    }
    select_combo(combo, 0);
}

int text_target_index(std::wstring_view target) {
    if (target.empty()) return 0;
    const auto& targets = text_translation_targets();
    const auto found = std::find(targets.begin(), targets.end(), target);
    return found == targets.end()
        ? 0 : static_cast<int>(std::distance(targets.begin(), found)) + 1;
}

std::wstring text_target_at(HWND combo) {
    const int selected = selected_index(combo);
    if (selected <= 0) return {};
    const auto& targets = text_translation_targets();
    const auto index = static_cast<std::size_t>(selected - 1);
    return index < targets.size() ? std::wstring(targets[index]) : std::wstring{};
}

std::wstring text_target_selector(const TextTranslationSession& session) {
    if (!session.target().empty()) return target_display_name(session.target());
    if (session.input().find_first_not_of(L" \t\r\n") == std::wstring::npos) {
        return L"自动选择";
    }
    return L"自动 · " + target_display_name(session.effective_target());
}

std::wstring text_translation_status(const TextTranslationSession& session) {
    switch (session.state()) {
    case TextTranslationState::idle:
        return L"等待输入";
    case TextTranslationState::waiting:
        return L"将在停止输入 500 毫秒后翻译";
    case TextTranslationState::translating:
        return L"正在翻译为" + target_display_name(session.effective_target()) + L"…";
    case TextTranslationState::ready:
        return L"已翻译为" + target_display_name(session.effective_target());
    case TextTranslationState::error:
        return session.error().empty() ? L"文字翻译失败" : session.error();
    case TextTranslationState::too_long:
        return L"最多支持 5000 个字符，请删减后重试";
    }
    return {};
}

int CALLBACK collect_font_family(const LOGFONTW* font, const TEXTMETRICW*,
                                 DWORD, LPARAM data) {
    if (!font || font->lfFaceName[0] == L'@') return 1;
    auto* families = reinterpret_cast<std::set<std::wstring, std::less<>>*>(data);
    families->insert(font->lfFaceName);
    return 1;
}

std::vector<std::wstring> installed_chinese_fonts() {
    std::set<std::wstring, std::less<>> families;
    HDC dc = GetDC(nullptr);
    if (dc) {
        LOGFONTW query{};
        query.lfCharSet = GB2312_CHARSET;
        EnumFontFamiliesExW(dc, &query, collect_font_family,
                            reinterpret_cast<LPARAM>(&families), 0);
        ReleaseDC(nullptr, dc);
    }
    if (families.empty()) families.insert(L"Microsoft YaHei UI");
    return {families.begin(), families.end()};
}

bool is_modifier_key(UINT key) noexcept {
    return key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
           key == VK_MENU || key == VK_LMENU || key == VK_RMENU ||
           key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
           key == VK_LWIN || key == VK_RWIN;
}

std::wstring hotkey_key_name(UINT key) {
    if ((key >= L'A' && key <= L'Z') || (key >= L'0' && key <= L'9')) {
        return std::wstring(1, static_cast<wchar_t>(key));
    }
    if (key >= VK_F1 && key <= VK_F24) {
        return L"F" + std::to_wstring(key - VK_F1 + 1);
    }
    switch (key) {
    case VK_SPACE: return L"Space";
    case VK_TAB: return L"Tab";
    case VK_RETURN: return L"Enter";
    case VK_BACK: return L"Backspace";
    case VK_DELETE: return L"Delete";
    case VK_INSERT: return L"Insert";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"PageUp";
    case VK_NEXT: return L"PageDown";
    case VK_UP: return L"Up";
    case VK_DOWN: return L"Down";
    case VK_LEFT: return L"Left";
    case VK_RIGHT: return L"Right";
    default: return {};
    }
}

bool autostart_enabled() {
    DWORD bytes = 0;
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"ScreenTranslate", RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
    return status == ERROR_SUCCESS && bytes > sizeof(wchar_t);
}

void set_autostart(bool enabled) {
    HKEY key{};
    const LSTATUS opened = RegCreateKeyExW(
        HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (opened != ERROR_SUCCESS) throw_last_error("open autostart registry", opened);
    struct RegistryKey {
        HKEY value{};
        ~RegistryKey() { if (value) RegCloseKey(value); }
    } guard{key};
    if (!enabled) {
        const LSTATUS removed = RegDeleteValueW(key, L"ScreenTranslate");
        if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND) {
            throw_last_error("disable autostart", removed);
        }
        return;
    }
    const auto command = L"\"" + executable_path().wstring() + L"\"";
    const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const LSTATUS written = RegSetValueExW(
        key, L"ScreenTranslate", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()), bytes);
    if (written != ERROR_SUCCESS) throw_last_error("enable autostart", written);
}

std::wstring ocr_language_name(std::wstring_view tag) {
    if (tag.starts_with(L"zh-Hans")) return L"简体中文";
    if (tag.starts_with(L"zh-Hant")) return L"繁体中文";
    if (tag.starts_with(L"en")) return L"英语";
    if (tag.starts_with(L"ja")) return L"日语";
    if (tag.starts_with(L"ko")) return L"韩语";
    if (tag.starts_with(L"fr")) return L"法语";
    if (tag.starts_with(L"de")) return L"德语";
    if (tag.starts_with(L"es")) return L"西班牙语";
    if (tag.starts_with(L"ru")) return L"俄语";
    if (tag.starts_with(L"it")) return L"意大利语";
    if (tag.starts_with(L"pt")) return L"葡萄牙语";
    return std::wstring(tag);
}

bool same_hotkey(const HotkeySpec& left, const HotkeySpec& right) noexcept {
    return left.modifiers == right.modifiers &&
           left.virtual_key == right.virtual_key;
}

}  // namespace

SettingsWindow::SettingsWindow(HINSTANCE instance, ConfigStore& config)
    : instance_(instance), config_(config) {}

SettingsWindow::~SettingsWindow() {
    cancel_model_refresh();
    if (test_thread_.joinable()) {
        test_thread_.request_stop();
        test_thread_.join();
    }
    discard_connection_test_results();
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    if (font_) DeleteObject(font_);
    if (title_font_) DeleteObject(title_font_);
    if (small_font_) DeleteObject(small_font_);
    if (icon_font_) DeleteObject(icon_font_);
    if (background_) DeleteObject(background_);
    if (sidebar_background_) DeleteObject(sidebar_background_);
    if (card_background_) DeleteObject(card_background_);
    if (input_background_) DeleteObject(input_background_);
}

bool SettingsWindow::preprocess_message(MSG& message) {
    const HWND dialog = window_;
    if (!dialog || !IsWindow(dialog) || !IsWindowVisible(dialog)) return false;
    if (current_page_ == 5 && message.message == WM_KEYDOWN &&
        message.wParam == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (text_callbacks_.translate_now) text_callbacks_.translate_now();
        return true;
    }
    return IsDialogMessageW(dialog, &message) != FALSE;
}

void SettingsWindow::show(HWND owner) {
    if (window_) {
        ShowWindow(window_, SW_RESTORE);
        SetForegroundWindow(window_);
        return;
    }
    owner_ = owner;
    finished_ = false;
    test_pending_ = false;
    model_refreshing_ = false;
    create_window(owner);
    const HWND window = window_;
    if (!window) throw AppError("settings window creation returned no window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
}

void SettingsWindow::show_translation(HWND owner) {
    show(owner);
    if (!window_) return;
    show_page(1);
    layout_controls();
    InvalidateRect(window_, nullptr, TRUE);
}

void SettingsWindow::attach_text_translation(
        TextTranslationSession& session, TextTranslationCallbacks callbacks) {
    text_session_ = &session;
    text_callbacks_ = std::move(callbacks);
    refresh_text_translation();
}

void SettingsWindow::detach_text_translation() noexcept {
    text_callbacks_ = {};
    text_session_ = nullptr;
}

void SettingsWindow::show_text_translation(HWND owner) {
    show(owner);
    if (!window_) return;
    show_page(5);
    refresh_text_translation();
    if (text_input_) SetFocus(text_input_);
}

void SettingsWindow::refresh_text_translation() {
    if (!window_ || !text_session_ || !text_input_ || !text_output_ ||
        !text_target_) {
        return;
    }
    syncing_text_ = true;
    if (text_of(text_input_) != text_session_->input()) {
        SetWindowTextW(text_input_, text_session_->input().c_str());
    }
    if (text_of(text_output_) != text_session_->output()) {
        SetWindowTextW(text_output_, text_session_->output().c_str());
    }
    const int expected_target = text_target_index(text_session_->target());
    if (selected_index(text_target_) != expected_target) {
        select_combo(text_target_, expected_target);
    }
    syncing_text_ = false;
    EnableWindow(text_copy_, !text_session_->output().empty());
    EnableWindow(text_clear_, !text_session_->input().empty() ||
                              !text_session_->output().empty());
    EnableWindow(text_translate_, text_session_->can_submit());
    InvalidateRect(text_target_, nullptr, FALSE);
    if (current_page_ == 5) InvalidateRect(window_, nullptr, FALSE);
}

void SettingsWindow::self_test(HWND owner) {
    if (window_) throw AppError("settings self-test window already exists");
    create_window(owner);
    if (!window_ || !capture_hotkey_ || !toggle_hotkey_ || !text_hotkey_ ||
        !provider_ || !ocr_engine_value_ || !navigation_[0] || !navigation_[1] ||
        !text_session_ || !text_target_ || !text_input_ || !text_output_ ||
        !text_translate_ || !text_clear_ || !text_copy_) {
        throw AppError("settings self-test did not create required controls");
    }
    constexpr std::array<std::wstring_view, 6> expected_navigation{
        L"文本翻译", L"截图识别", L"翻译接口",
        L"快捷键", L"显示", L"其他",
    };
    for (std::size_t index = 0; index < expected_navigation.size(); ++index) {
        if (!navigation_[index] ||
            text_of(navigation_[index]) != expected_navigation[index]) {
            throw AppError("settings self-test navigation order mismatch");
        }
    }
    constexpr LONG_PTR required_extended_style = WS_EX_APPWINDOW | WS_EX_COMPOSITED;
    if ((GetWindowLongPtrW(window_, GWL_EXSTYLE) & required_extended_style) !=
            required_extended_style ||
        GetWindow(window_, GW_OWNER) != nullptr || !icon_font_) {
        throw AppError("settings self-test did not create an independent composited window");
    }
    RECT client{};
    GetClientRect(window_, &client);
    if (std::abs(client.right - px(790, dpi_)) > 2 ||
        std::abs(client.bottom - px(610, dpi_)) > 2) {
        throw AppError("settings self-test client size differs from Python reference");
    }
    if ((GetWindowLongPtrW(status_, GWL_STYLE) & WS_VISIBLE) != 0) {
        throw AppError("settings self-test empty footer status remained visible");
    }
    set_status(L"status visibility test", true);
    if ((GetWindowLongPtrW(status_, GWL_STYLE) & WS_VISIBLE) == 0) {
        throw AppError("settings self-test non-empty footer status remained hidden");
    }
    set_status(L"");
    if ((GetWindowLongPtrW(status_, GWL_STYLE) & WS_VISIBLE) != 0) {
        throw AppError("settings self-test cleared footer status remained visible");
    }
    if (SendMessageW(provider_, CB_GETCOUNT, 0, 0) !=
        static_cast<LRESULT>(providers.size() - 1)) {
        throw AppError("settings self-test provider count mismatch");
    }
    constexpr std::array<std::wstring_view, 5> expected_providers{
        L"腾讯交互翻译（免密钥）", L"微软翻译（免密钥）", L"词霸翻译（免密钥）",
        L"Yandex 翻译（免密钥）", L"DeepL",
    };
    for (std::size_t index = 0; index < expected_providers.size(); ++index) {
        wchar_t label[128]{};
        SendMessageW(provider_, CB_GETLBTEXT, static_cast<WPARAM>(index),
                     reinterpret_cast<LPARAM>(label));
        if (std::wstring_view(label) != expected_providers[index]) {
            throw AppError("settings self-test provider ordering mismatch");
        }
    }
    if (SendMessageW(translator_class_, CB_GETCOUNT, 0, 0) != 2) {
        throw AppError("settings self-test translator class count mismatch");
    }
    if (SendMessageW(ai_protocol_, CB_GETCOUNT, 0, 0) !=
        static_cast<LRESULT>(ai_protocols.size())) {
        throw AppError("settings self-test large-model protocol count mismatch");
    }
    wchar_t ocr_engine_class[16]{};
    if (!GetClassNameW(ocr_engine_value_, ocr_engine_class,
                       static_cast<int>(std::size(ocr_engine_class))) ||
        std::wstring_view(ocr_engine_class) != L"Static" ||
        text_of(ocr_engine_value_) != L"系统自带 OCR") {
        throw AppError("settings self-test OCR engine display mismatch");
    }
    show_page(0);
    if (page_controls_[0].size() < 3 ||
        text_of(page_controls_[0][2]) != L"收起 / 显示") {
        throw AppError("settings self-test hotkey label differs from Python reference");
    }
    if (page_controls_[0].size() < 7 ||
        text_of(page_controls_[0][4]) != L"快速文字翻译" ||
        text_of(text_hotkey_) != L"Ctrl+Alt+Space") {
        throw AppError("settings self-test quick translation hotkey is missing");
    }
    std::wstring hotkey_error;
    const auto capture_spec = parse_hotkey(L"Ctrl+Alt+Q", &hotkey_error);
    const auto toggle_spec = parse_hotkey(L"Ctrl+Alt+W", &hotkey_error);
    const auto text_spec = parse_hotkey(L"Ctrl+Alt+Space", &hotkey_error);
    const auto equivalent_text_spec = parse_hotkey(L"control+alt+space", &hotkey_error);
    const auto input_method_spec = parse_hotkey(L"Ctrl+Space", &hotkey_error);
    if (!capture_spec || !toggle_spec || !text_spec || !equivalent_text_spec ||
        !input_method_spec || same_hotkey(*capture_spec, *toggle_spec) ||
        same_hotkey(*capture_spec, *text_spec) ||
        same_hotkey(*toggle_spec, *text_spec) ||
        !same_hotkey(*text_spec, *equivalent_text_spec)) {
        throw AppError("settings self-test hotkey duplicate detection mismatch");
    }
    RECT hotkey_rect{};
    GetWindowRect(capture_hotkey_, &hotkey_rect);
    const int hotkey_width = hotkey_rect.right - hotkey_rect.left;
    const int hotkey_height = hotkey_rect.bottom - hotkey_rect.top;
    const int expected_hotkey_height = px(34, dpi_) -
        std::max(1, px(logical_edit_inset_top, dpi_)) -
        std::max(1, px(logical_edit_inset_bottom, dpi_));
    RECT toggle_rect{};
    RECT text_rect{};
    if (!GetWindowRect(toggle_hotkey_, &toggle_rect) ||
        !GetWindowRect(text_hotkey_, &text_rect) ||
        std::abs(hotkey_width - (toggle_rect.right - toggle_rect.left)) > 1 ||
        std::abs(hotkey_width - (text_rect.right - text_rect.left)) > 1 ||
        std::abs(hotkey_rect.right - toggle_rect.right) > 1 ||
        std::abs(hotkey_rect.right - text_rect.right) > 1 ||
        std::abs(hotkey_height - expected_hotkey_height) > 2) {
        throw AppError("settings self-test hotkey text inset mismatch");
    }
    if ((GetWindowLongPtrW(capture_hotkey_, GWL_STYLE) & WS_BORDER) != 0 ||
        (GetWindowLongPtrW(capture_hotkey_, GWL_EXSTYLE) &
             (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE)) != 0) {
        throw AppError("settings self-test edit field retained a square system frame");
    }
    HRGN hotkey_region = CreateRectRgn(0, 0, 1, 1);
    const int region_type = hotkey_region
        ? GetWindowRgn(capture_hotkey_, hotkey_region) : ERROR;
    RECT hotkey_client{};
    GetClientRect(capture_hotkey_, &hotkey_client);
    const bool rounded_region = region_type == COMPLEXREGION &&
        !PtInRegion(hotkey_region, 0, 0) &&
        PtInRegion(hotkey_region, hotkey_client.right / 2,
                   hotkey_client.bottom / 2);
    if (hotkey_region) DeleteObject(hotkey_region);
    if (!rounded_region) {
        throw AppError("settings self-test edit field did not receive rounded corners");
    }
    show_page(5);
    if (SendMessageW(text_target_, CB_GETCOUNT, 0, 0) != 10) {
        throw AppError("settings self-test text translation page is incomplete");
    }
    COMBOBOXINFO text_target_info{sizeof(text_target_info)};
    RECT text_target_list{};
    if (!GetComboBoxInfo(text_target_, &text_target_info) ||
        !text_target_info.hwndList ||
        !GetWindowRect(text_target_info.hwndList, &text_target_list) ||
        text_target_list.bottom - text_target_list.top < px(200, dpi_)) {
        throw AppError("settings self-test text target menu is clipped");
    }
    RECT text_input_rect{};
    RECT text_output_rect{};
    if (!GetWindowRect(text_input_, &text_input_rect) ||
        !GetWindowRect(text_output_, &text_output_rect) ||
        text_input_rect.right - text_input_rect.left < px(480, dpi_) ||
        text_output_rect.bottom - text_output_rect.top < px(120, dpi_)) {
        throw AppError("settings self-test text translation layout is invalid");
    }
    show_page(1);
    SendMessageW(translator_class_, CB_SETCURSEL, 1, 0);
    refresh_provider_fields(false);
    COMBOBOXINFO model_info{sizeof(model_info)};
    RECT model_combo_rect{};
    RECT model_edit_rect{};
    if (!GetComboBoxInfo(provider_model_, &model_info) || !model_info.hwndItem ||
        !GetWindowRect(provider_model_, &model_combo_rect) ||
        !GetWindowRect(model_info.hwndItem, &model_edit_rect)) {
        throw AppError("settings self-test could not inspect model combo edit field");
    }
    DWORD_PTR combo_edit_reference = 0;
    if (!GetWindowSubclass(model_info.hwndItem,
                           &SettingsWindow::combo_edit_proc,
                           combo_edit_subclass_id, &combo_edit_reference) ||
        combo_edit_reference != reinterpret_cast<DWORD_PTR>(this)) {
        throw AppError("settings self-test model combo edit listener is missing");
    }
    const int model_edit_top = model_edit_rect.top - model_combo_rect.top;
    if (model_edit_top < px(logical_combo_edit_offset_y + 1, dpi_)) {
        throw AppError("settings self-test model combo text remained top aligned");
    }
    align_combo_edit(provider_model_, dpi_);
    RECT realigned_model_edit_rect{};
    if (!GetWindowRect(model_info.hwndItem, &realigned_model_edit_rect) ||
        std::abs(realigned_model_edit_rect.top - model_edit_rect.top) > 1 ||
        std::abs(realigned_model_edit_rect.bottom - model_edit_rect.bottom) > 1) {
        throw AppError("settings self-test model combo alignment accumulated");
    }
    const int model_edit_width = model_edit_rect.right - model_edit_rect.left;
    const int model_edit_height = model_edit_rect.bottom - model_edit_rect.top;
    SetWindowPos(model_info.hwndItem, nullptr, 0, 0,
                 model_edit_width, model_edit_height,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    RECT restored_model_edit_rect{};
    if (!GetWindowRect(model_info.hwndItem, &restored_model_edit_rect) ||
        std::abs(restored_model_edit_rect.left - model_edit_rect.left) > 1 ||
        std::abs(restored_model_edit_rect.top - model_edit_rect.top) > 1 ||
        std::abs(restored_model_edit_rect.right - model_edit_rect.right) > 1 ||
        std::abs(restored_model_edit_rect.bottom - model_edit_rect.bottom) > 1) {
        throw AppError("settings self-test model combo focus reset was not repaired");
    }
    RECT test_button_rect{};
    RECT test_status_rect{};
    if (!GetWindowRect(test_button_, &test_button_rect) ||
        !GetWindowRect(test_status_, &test_status_rect) ||
        (GetWindowLongPtrW(test_status_, GWL_STYLE) & SS_CENTERIMAGE) == 0 ||
        std::abs(test_button_rect.top - test_status_rect.top) > 1 ||
        std::abs(test_button_rect.bottom - test_status_rect.bottom) > 1) {
        throw AppError("settings self-test connection status is not button aligned");
    }
    MSG pending_selection_clear{};
    while (PeekMessageW(&pending_selection_clear, window_,
                        clear_combo_selection_message,
                        clear_combo_selection_message, PM_REMOVE)) {}
    constexpr std::wstring_view selection_test_model = L"selection-test-model";
    SetWindowTextW(provider_model_, selection_test_model.data());
    set_combo_edit_redraw(provider_model_, false);
    SendMessageW(model_info.hwndItem, EM_SETSEL, 0, -1);
    clear_combo_edit_selection(provider_model_);
    set_combo_edit_redraw(provider_model_, true);
    PostMessageW(window_, clear_combo_selection_message,
                 reinterpret_cast<WPARAM>(provider_model_), 0);
    if (!PeekMessageW(&pending_selection_clear, window_,
                      clear_combo_selection_message,
                      clear_combo_selection_message, PM_REMOVE)) {
        throw AppError("settings self-test model selection clear was not deferred");
    }
    DispatchMessageW(&pending_selection_clear);
    DWORD selection_start = 0;
    DWORD selection_end = 0;
    SendMessageW(model_info.hwndItem, EM_GETSEL,
                 reinterpret_cast<WPARAM>(&selection_start),
                 reinterpret_cast<LPARAM>(&selection_end));
    if (selection_start != selection_end ||
        selection_end != selection_test_model.size()) {
        throw AppError("settings self-test default model remained selected");
    }
    SendMessageW(translator_class_, CB_SETCURSEL, 0, 0);
    SendMessageW(provider_, CB_SETCURSEL, 0, 0);
    refresh_provider_fields(true);
    SendMessageW(provider_, CB_SETCURSEL, 2, 0);
    refresh_provider_fields(true);
    if ((GetWindowLongPtrW(provider_key_, GWL_STYLE) & WS_VISIBLE) != 0 ||
        (GetWindowLongPtrW(provider_model_, GWL_STYLE) & WS_VISIBLE) != 0 ||
        (GetWindowLongPtrW(provider_endpoint_, GWL_STYLE) & WS_VISIBLE) != 0 ||
        (GetWindowLongPtrW(model_refresh_, GWL_STYLE) & WS_VISIBLE) != 0) {
        throw AppError("settings self-test retained AI fields for a free provider");
    }
    show_page(2);
    if ((GetWindowLongPtrW(ocr_languages_, GWL_STYLE) & WS_VISIBLE) == 0) {
        throw AppError("settings self-test hid the offline OCR language list");
    }
    RECT ocr_engine_rect{};
    RECT ocr_note_rect{};
    RECT ocr_languages_rect{};
    if (!GetWindowRect(ocr_engine_value_, &ocr_engine_rect) ||
        !GetWindowRect(ocr_engine_note_, &ocr_note_rect) ||
        !GetWindowRect(ocr_languages_, &ocr_languages_rect) ||
        ocr_note_rect.top < ocr_engine_rect.bottom ||
        ocr_note_rect.bottom > ocr_languages_rect.top ||
        ocr_note_rect.bottom - ocr_note_rect.top < px(20, dpi_)) {
        throw AppError("settings self-test OCR note is clipped");
    }
    show_page(3);
    layout_controls();
    RECT copy_label_rect{};
    RECT copy_control_rect{};
    RECT close_control_rect{};
    if (page_controls_[3].size() < 8 ||
        text_of(page_controls_[3][7]) != L"复制译文" ||
        !GetWindowRect(page_controls_[3][7], &copy_label_rect) ||
        !GetWindowRect(auto_copy_, &copy_control_rect) ||
        !GetWindowRect(close_value_, &close_control_rect) ||
        text_of(close_value_) != L"点击关闭或按ESC" ||
        std::abs(copy_label_rect.top - copy_control_rect.top) > 1 ||
        std::abs(copy_label_rect.bottom - copy_control_rect.bottom) > 1 ||
        copy_control_rect.left != close_control_rect.left ||
        copy_control_rect.top <= close_control_rect.bottom) {
        throw AppError("settings self-test appearance rows are not aligned");
    }
    show_page(4);
    if (text_of(diagnostic_command_) != L"ScreenTranslate.exe --selftest") {
        throw AppError("settings self-test diagnostic command mismatch");
    }
    if (!check_updates_ || text_of(check_updates_) != L"检查更新" ||
        (GetWindowLongPtrW(check_updates_, GWL_STYLE) & WS_VISIBLE) == 0) {
        throw AppError("settings self-test update command is missing");
    }
    DestroyWindow(window_);
    window_ = nullptr;
}

void SettingsWindow::create_window(HWND owner) {
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = &SettingsWindow::window_proc;
    description.style = 0;
    description.hInstance = instance_;
    description.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    description.hIconSm = description.hIcon;
    description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    description.lpszClassName = class_name;
    if (!RegisterClassExW(&description) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw_last_error("register settings window");
    }
    RECT owner_rect{};
    const bool usable_owner = owner && IsWindowVisible(owner) && GetWindowRect(owner, &owner_rect) &&
        owner_rect.right - owner_rect.left >= 100 && owner_rect.bottom - owner_rect.top >= 100;
    HMONITOR monitor{};
    if (usable_owner) {
        monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    } else {
        POINT cursor{};
        GetCursorPos(&cursor);
        monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{sizeof(info)};
        if (GetMonitorInfoW(monitor, &info)) {
            owner_rect = info.rcWork;
        } else {
            owner_rect = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        }
    }
    UINT monitor_dpi_x = 96;
    UINT monitor_dpi_y = 96;
    if (!monitor || FAILED(GetDpiForMonitor(
            monitor, MDT_EFFECTIVE_DPI, &monitor_dpi_x, &monitor_dpi_y))) {
        monitor_dpi_x = GetDpiForSystem();
    }
    dpi_ = usable_owner ? static_cast<int>(GetDpiForWindow(owner))
                        : static_cast<int>(monitor_dpi_x);
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                        WS_THICKFRAME | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    // Page changes show, hide, and move several native child controls. Compose
    // the complete descendant tree before presenting it so owner-drawn buttons
    // and editable combo boxes never expose their default light erase frame.
    constexpr DWORD extended_style = WS_EX_APPWINDOW | WS_EX_COMPOSITED;
    RECT desired{0, 0, px(790, dpi_), px(610, dpi_)};
    AdjustWindowRectExForDpi(&desired, style, FALSE, extended_style,
                             static_cast<UINT>(dpi_));
    const int width = desired.right - desired.left;
    const int height = desired.bottom - desired.top;
    const int x = owner_rect.left + std::max(0L, (owner_rect.right - owner_rect.left - width) / 2);
    const int y = owner_rect.top + std::max(0L, (owner_rect.bottom - owner_rect.top - height) / 2);
    window_error_.clear();
    window_ = CreateWindowExW(
        extended_style, class_name, L"划词截屏翻译",
        style,
        x, y, width, height, nullptr, nullptr, instance_, this);
    if (!window_) {
        if (!window_error_.empty()) throw AppError(window_error_);
        throw_last_error("create settings window");
    }
    enable_dark_frame(window_);
}

HWND SettingsWindow::add_control(int page, DWORD ex_style, const wchar_t* type,
                                 const wchar_t* text, DWORD style, int identifier) {
    if (_wcsicmp(type, L"EDIT") == 0) {
        // A native EDIT can paint its classic square non-client frame once
        // before a later style change takes effect. Create it borderless from
        // the outset; the parent paints the shared antialiased rounded frame.
        style &= ~WS_BORDER;
        ex_style &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
    }
    HWND control = CreateWindowExW(
        ex_style, type, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 1, 1, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)), instance_, nullptr);
    if (!control) throw_last_error("create settings control");
    set_font(control, font_);
    apply_control_theme(control);
    if (page >= 0) page_controls_[static_cast<std::size_t>(page)].push_back(control);
    return control;
}

HWND SettingsWindow::add_label(int page, const wchar_t* text) {
    return add_control(page, 0, L"STATIC", text, SS_LEFT, 0);
}

void SettingsWindow::create_theme_resources() {
    for (auto** font : {&font_, &title_font_, &small_font_, &icon_font_}) {
        if (*font) DeleteObject(*font);
        *font = nullptr;
    }
    for (auto** brush : {&background_, &sidebar_background_, &card_background_,
                         &input_background_}) {
        if (*brush) DeleteObject(*brush);
        *brush = nullptr;
    }

    const auto make_font = [&](int pixels, int weight) {
        return CreateFontW(-px(pixels, dpi_), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Microsoft YaHei UI");
    };
    font_ = make_font(13, FW_NORMAL);
    title_font_ = make_font(18, FW_SEMIBOLD);
    small_font_ = make_font(11, FW_NORMAL);
    icon_font_ = create_icon_font(dpi_);
    background_ = CreateSolidBrush(color_page);
    sidebar_background_ = CreateSolidBrush(color_sidebar);
    card_background_ = CreateSolidBrush(color_card);
    input_background_ = CreateSolidBrush(color_input);
}

void SettingsWindow::apply_control_theme(HWND control) {
    if (!control) return;
    wchar_t class_name_buffer[32]{};
    GetClassNameW(control, class_name_buffer, static_cast<int>(std::size(class_name_buffer)));
    if (_wcsicmp(class_name_buffer, L"Edit") == 0) {
        // The Explorer theme paints its own square edit frame even without
        // WS_BORDER.  The settings window supplies the shared rounded field.
        SetWindowTheme(control, L"", L"");
        const auto style = GetWindowLongPtrW(control, GWL_STYLE);
        SetWindowLongPtrW(control, GWL_STYLE,
                          style & ~static_cast<LONG_PTR>(WS_BORDER));
        const auto extended_style = GetWindowLongPtrW(control, GWL_EXSTYLE);
        SetWindowLongPtrW(control, GWL_EXSTYLE,
                          extended_style & ~static_cast<LONG_PTR>(WS_EX_CLIENTEDGE |
                                                                  WS_EX_STATICEDGE));
        SetWindowPos(control, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                          SWP_FRAMECHANGED);
        SendMessageW(control, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(px(9, dpi_), px(9, dpi_)));
        SetWindowSubclass(control, &SettingsWindow::edit_proc, edit_subclass_id,
                          reinterpret_cast<DWORD_PTR>(this));
    } else if (_wcsicmp(class_name_buffer, L"ComboBox") == 0) {
        SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
        COMBOBOXINFO info{sizeof(info)};
        if (GetComboBoxInfo(control, &info) && info.hwndItem && info.hwndItem != control) {
            SetWindowTheme(info.hwndItem, L"DarkMode_Explorer", nullptr);
            set_font(info.hwndItem, font_);
            SendMessageW(info.hwndItem, EM_SETMARGINS,
                         EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELPARAM(px(9, dpi_), px(9, dpi_)));
            SetWindowSubclass(info.hwndItem, &SettingsWindow::combo_edit_proc,
                              combo_edit_subclass_id,
                              reinterpret_cast<DWORD_PTR>(this));
        }
        style_combo_popup(control, dpi_, font_);
        SetWindowSubclass(control, &SettingsWindow::combo_proc, combo_subclass_id,
                          reinterpret_cast<DWORD_PTR>(this));
        align_combo_edit(control, dpi_);
    } else {
        SetWindowTheme(control, L"DarkMode_Explorer", nullptr);
    }
}

LRESULT CALLBACK SettingsWindow::edit_proc(HWND control, UINT message,
                                            WPARAM wparam, LPARAM lparam,
                                            UINT_PTR subclass_id, DWORD_PTR reference) {
    auto* self = reinterpret_cast<SettingsWindow*>(reference);
    const auto update_region = [&] {
        if (!self) return;
        RECT rect{};
        GetClientRect(control, &rect);
        if (rect.right <= rect.left || rect.bottom <= rect.top) return;
        const int inset = std::max(1, px(logical_edit_inset_x, self->dpi_));
        const int inner_radius = std::max(
            1, px(logical_edit_corner_radius, self->dpi_) - inset);
        const int diameter = inner_radius * 2;
        HRGN region = CreateRoundRectRgn(
            0, 0, rect.right + 1, rect.bottom + 1, diameter, diameter);
        if (region && !SetWindowRgn(control, region, TRUE)) DeleteObject(region);
    };
    switch (message) {
    case WM_NCPAINT:
        // The settings window owns the edit frame. Letting the EDIT class
        // repaint its non-client area would put a square system border over it.
        return 0;
    case WM_SIZE: {
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        update_region();
        return result;
    }
    case WM_IME_STARTCOMPOSITION:
        if (self && control == self->text_input_ &&
            self->text_callbacks_.composition_changed) {
            self->text_callbacks_.composition_changed(true);
        }
        break;
    case WM_IME_ENDCOMPOSITION:
        if (self && control == self->text_input_ &&
            self->text_callbacks_.composition_changed) {
            self->text_callbacks_.composition_changed(false);
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        if (self && self->window_) InvalidateRect(self->window_, nullptr, FALSE);
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, &SettingsWindow::edit_proc, subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT CALLBACK SettingsWindow::combo_proc(HWND control, UINT message,
                                            WPARAM wparam, LPARAM lparam,
                                            UINT_PTR subclass_id, DWORD_PTR reference) {
    auto* self = reinterpret_cast<SettingsWindow*>(reference);
    switch (message) {
    case WM_SIZE: {
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        if (self) align_combo_edit(control, self->dpi_);
        return result;
    }
    case WM_PAINT: {
        PAINTSTRUCT state{};
        HDC dc = BeginPaint(control, &state);
        if (dc && self) {
            try {
                self->draw_combo(control, dc);
            } catch (...) {
                RECT rect{};
                GetClientRect(control, &rect);
                FillRect(dc, &rect, self->input_background_);
            }
        }
        EndPaint(control, &state);
        return 0;
    }
    case WM_PRINTCLIENT:
        if (self) {
            try {
                self->draw_combo(control, reinterpret_cast<HDC>(wparam));
            } catch (...) {
                return 0;
            }
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        if (message == WM_SETFOCUS) {
            if (self) align_combo_edit(control, self->dpi_);
            clear_combo_edit_selection(control);
        }
        InvalidateRect(control, nullptr, FALSE);
        return result;
    }
    case WM_LBUTTONDOWN: {
        set_combo_edit_redraw(control, false);
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        if (self) align_combo_edit(control, self->dpi_);
        clear_combo_edit_selection(control);
        if (!SendMessageW(control, CB_GETDROPPEDSTATE, 0, 0)) {
            set_combo_edit_redraw(control, true);
        }
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, &SettingsWindow::combo_proc, subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT CALLBACK SettingsWindow::combo_edit_proc(HWND control, UINT message,
                                                 WPARAM wparam, LPARAM lparam,
                                                 UINT_PTR subclass_id,
                                                 DWORD_PTR reference) {
    auto* self = reinterpret_cast<SettingsWindow*>(reference);
    switch (message) {
    case WM_SETFOCUS:
    case WM_LBUTTONDOWN:
    case WM_WINDOWPOSCHANGED: {
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        const HWND combo = GetParent(control);
        if (self && combo) align_combo_edit(combo, self->dpi_);
        if (message == WM_SETFOCUS) {
            clear_combo_edit_selection(combo);
        }
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, &SettingsWindow::combo_edit_proc,
                             subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT CALLBACK SettingsWindow::hotkey_proc(HWND control, UINT message,
                                             WPARAM wparam, LPARAM lparam,
                                             UINT_PTR subclass_id, DWORD_PTR reference) {
    auto* self = reinterpret_cast<SettingsWindow*>(reference);
    try {
    switch (message) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;
    case WM_SETFOCUS: {
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        SendMessageW(control, EM_SETSEL, 0, -1);
        return result;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        const UINT key = static_cast<UINT>(wparam);
        if (is_modifier_key(key)) return 0;
        if (!self) return 0;
        if (key == VK_ESCAPE) {
            SetFocus(self->navigation_[navigation_index_for_page(self->current_page_)]);
            return 0;
        }
        const auto name = hotkey_key_name(key);
        if (name.empty()) {
            MessageBeep(MB_ICONWARNING);
            self->set_status(L"这个按键不能用作快捷键", true);
            return 0;
        }
        std::wstring value;
        const auto append = [&](std::wstring_view part) {
            if (!value.empty()) value += L'+';
            value += part;
        };
        if (GetKeyState(VK_CONTROL) < 0) append(L"Ctrl");
        if (GetKeyState(VK_MENU) < 0) append(L"Alt");
        if (GetKeyState(VK_SHIFT) < 0) append(L"Shift");
        if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) append(L"Win");
        const bool function_key = key >= VK_F1 && key <= VK_F24;
        if (value.empty() && !function_key) {
            MessageBeep(MB_ICONWARNING);
            self->set_status(L"除 F1 到 F24 外，快捷键必须带 Ctrl、Alt、Shift 或 Win", true);
            return 0;
        }
        append(name);
        SetWindowTextW(control, value.c_str());
        self->save_values(false);
        SetFocus(self->navigation_[navigation_index_for_page(self->current_page_)]);
        return 0;
    }
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_PASTE:
    case WM_CUT:
    case WM_CLEAR:
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, &SettingsWindow::hotkey_proc, subclass_id);
        break;
    default:
        break;
    }
    } catch (...) {
        if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
            MessageBeep(MB_ICONWARNING);
            return 0;
        }
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

void SettingsWindow::draw_combo(HWND control, HDC dc) {
    if (!dc) return;
    RECT rect{};
    GetClientRect(control, &rect);
    FillRect(dc, &rect, card_background_);
    const bool enabled = IsWindowEnabled(control) != FALSE;
    const bool focused = GetFocus() == control || IsChild(control, GetFocus());
    wchar_t accent_text[16]{L'#', L'2', L'8', L'C', L'7', L'6', L'F', L'\0'};
    if (accent_) GetWindowTextW(accent_, accent_text, static_cast<int>(std::size(accent_text)));
    const COLORREF accent = parse_rgb_color(accent_text);
    fill_round_rect(dc, rect, px(6, dpi_), color_input,
                    focused ? accent : color_line);

    RECT text_rect = rect;
    text_rect.left += px(9, dpi_);
    text_rect.right -= px(32, dpi_);
    OffsetRect(&text_rect, 0, px(logical_field_text_offset_y, dpi_));
    const std::wstring value = control == text_target_ && text_session_
        ? text_target_selector(*text_session_) : text_of(control);
    draw_text(dc, value, text_rect, font_,
              enabled ? color_text : color_text_faint,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    HPEN pen = CreatePen(PS_SOLID, std::max(1, px(2, dpi_)),
                         enabled ? color_text_dim : color_text_faint);
    const auto old_pen = SelectObject(dc, pen);
    const int x = rect.right - px(16, dpi_);
    const int y = (rect.top + rect.bottom) / 2;
    MoveToEx(dc, x - px(4, dpi_), y - px(2, dpi_), nullptr);
    LineTo(dc, x, y + px(2, dpi_));
    LineTo(dc, x + px(4, dpi_), y - px(2, dpi_));
    SelectObject(dc, old_pen);
    DeleteObject(pen);
}

bool SettingsWindow::is_checkbox(HWND control) const noexcept {
    return control && (control == ocr_upscale_ || control == auto_copy_ ||
                       control == autostart_);
}

bool SettingsWindow::checkbox_checked(HWND control) const noexcept {
    return GetPropW(control, L"ScreenTranslate.Checked") != nullptr;
}

void SettingsWindow::set_checkbox(HWND control, bool checked) {
    if (!control) return;
    if (checked) SetPropW(control, L"ScreenTranslate.Checked", reinterpret_cast<HANDLE>(1));
    else RemovePropW(control, L"ScreenTranslate.Checked");
    InvalidateRect(control, nullptr, FALSE);
}

void SettingsWindow::create_controls() {
    create_theme_resources();
    test_success_ = false;
    test_pending_ = false;
    model_refreshing_ = false;
    for (auto& controls : page_controls_) controls.clear();
    navigation_.fill(nullptr);
    for (std::size_t index = 0; index < navigation_.size(); ++index) {
        const int page = navigation_page_order[index];
        navigation_[index] = add_control(
            -1, 0, L"BUTTON", page_names[static_cast<std::size_t>(page)].data(),
            BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP, id_nav_first + static_cast<int>(index));
    }
    status_ = add_control(-1, 0, L"STATIC", L"", SS_LEFT, 0);
    set_font(status_, small_font_);
    ShowWindow(status_, SW_HIDE);
    create_hotkey_page();
    create_translation_page();
    create_ocr_page();
    create_appearance_page();
    create_other_page();
    create_text_translation_page();
    load_values();
    refresh_text_translation();
    // Open on the sidebar's first entry so the highlighted button always
    // matches the page the user sees.
    show_page(navigation_page_order.front());
    layout_controls();
}

void SettingsWindow::create_hotkey_page() {
    add_label(0, L"框选翻译");
    capture_hotkey_ = add_control(0, 0, L"EDIT", L"",
                                  ES_AUTOHSCROLL | ES_CENTER | ES_READONLY |
                                  WS_BORDER | WS_TABSTOP);
    add_label(0, L"收起 / 显示");
    toggle_hotkey_ = add_control(0, 0, L"EDIT", L"",
                                 ES_AUTOHSCROLL | ES_CENTER | ES_READONLY |
                                 WS_BORDER | WS_TABSTOP);
    add_label(0, L"快速文字翻译");
    text_hotkey_ = add_control(0, 0, L"EDIT", L"",
                               ES_AUTOHSCROLL | ES_CENTER | ES_READONLY |
                               WS_BORDER | WS_TABSTOP);
    SetWindowSubclass(capture_hotkey_, &SettingsWindow::hotkey_proc,
                      hotkey_subclass_id, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(toggle_hotkey_, &SettingsWindow::hotkey_proc,
                      hotkey_subclass_id, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(text_hotkey_, &SettingsWindow::hotkey_proc,
                      hotkey_subclass_id, reinterpret_cast<DWORD_PTR>(this));
    add_label(0, L"拖动鼠标    画出要翻译的区域，松手立刻识别并翻译\r\nEsc / 右键    取消这次框选");
}

void SettingsWindow::create_translation_page() {
    add_label(1, L"接口类别");
    translator_class_ = add_control(1, 0, L"COMBOBOX", L"",
                                    CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                                    WS_VSCROLL | WS_TABSTOP,
                                    id_translator_class);
    SendMessageW(translator_class_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"传统翻译 API"));
    SendMessageW(translator_class_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"大模型翻译 API"));
    provider_label_ = add_label(1, L"接口");
    provider_ = add_control(1, 0, L"COMBOBOX", L"",
                            CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                            WS_VSCROLL | WS_TABSTOP,
                            id_provider);
    for (const auto& provider : providers) {
        if (provider.kind == ProviderInfo::Kind::ai) continue;
        SendMessageW(provider_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(provider.label.data()));
    }
    profile_label_ = add_label(1, L"接口名称");
    profile_select_ = add_control(1, 0, L"COMBOBOX", L"",
                                  CBS_DROPDOWN | CBS_AUTOHSCROLL |
                                      CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                                      WS_VSCROLL | WS_TABSTOP);
    profile_add_ = add_control(1, 0, L"BUTTON", L"新增",
                               BS_OWNERDRAW | WS_TABSTOP, id_profile_add);
    profile_remove_ = add_control(1, 0, L"BUTTON", L"删除",
                                  BS_OWNERDRAW | WS_TABSTOP, id_profile_remove);
    protocol_label_ = add_label(1, L"接口协议");
    ai_protocol_ = add_control(1, 0, L"COMBOBOX", L"",
                               CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                               WS_VSCROLL | WS_TABSTOP,
                               id_ai_protocol);
    for (const auto& [id, label] : ai_protocols) {
        SendMessageW(ai_protocol_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(label.data()));
    }
    provider_key_label_ = add_label(1, L"API Key");
    provider_key_ = add_control(1, 0, L"EDIT", L"",
                                ES_AUTOHSCROLL | ES_PASSWORD | WS_BORDER | WS_TABSTOP);
    provider_endpoint_label_ = add_label(1, L"Base URL");
    provider_endpoint_ = add_control(1, 0, L"EDIT", L"",
                                     ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP);
    provider_model_label_ = add_label(1, L"模型");
    provider_model_ = add_control(1, 0, L"COMBOBOX", L"",
                                  CBS_DROPDOWN | CBS_AUTOHSCROLL |
                                      CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                                      WS_VSCROLL | WS_TABSTOP);
    model_refresh_ = add_control(1, 0, L"BUTTON", L"获取模型",
                                 BS_OWNERDRAW | WS_TABSTOP, id_refresh_models);
    test_button_ = add_control(1, 0, L"BUTTON", L"测试连接",
                               BS_OWNERDRAW | WS_TABSTOP, id_test);
    test_status_ = add_control(1, 0, L"STATIC", L"",
                               SS_LEFT | SS_CENTERIMAGE, 0);
    set_font(test_status_, small_font_);
    provider_hint_ = add_label(1, L"");
    set_font(provider_hint_, small_font_);
    add_label(1, L"中文译成");
    chinese_target_ = add_control(1, 0, L"COMBOBOX", L"",
                                  CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                                  WS_VSCROLL | WS_TABSTOP);
    for (const auto& [code, label] : zh_targets) {
        SendMessageW(chinese_target_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.data()));
    }
}

void SettingsWindow::create_ocr_page() {
    add_label(2, L"引擎");
    // Windows OCR is the only engine, so present it as a plain value instead of
    // a one-item drop-down that implies a choice.
    ocr_engine_value_ = add_control(
        2, 0, L"STATIC", L"系统自带 OCR", SS_LEFT | SS_CENTERIMAGE, 0);
    ocr_engine_note_ = add_label(2, L"截图只在本地识别，不会上传到任何服务器。");
    SetWindowLongPtrW(ocr_engine_note_, GWL_STYLE,
                      (GetWindowLongPtrW(ocr_engine_note_, GWL_STYLE) &
                       ~static_cast<LONG_PTR>(SS_TYPEMASK)) |
                          SS_LEFT | SS_CENTERIMAGE);
    set_font(ocr_engine_note_, small_font_);
    add_label(2, L"识别语言");
    ocr_languages_ = add_control(2, 0, L"COMBOBOX", L"",
                                 CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
                                 CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP);
    std::vector<std::wstring> installed;
    try {
        for (const auto& language :
             winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages()) {
            installed.emplace_back(language.LanguageTag().c_str());
        }
    } catch (...) {
        // The rest of the settings window remains usable even if WinRT language discovery fails.
    }
    std::stable_partition(installed.begin(), installed.end(), [](const std::wstring& tag) {
        return tag.starts_with(L"zh") || tag.starts_with(L"ja") || tag.starts_with(L"ko");
    });
    ocr_language_options_.clear();
    if (!installed.empty()) {
        ocr_language_options_.push_back(installed);
        SendMessageW(ocr_languages_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"自动（推荐）"));
        for (const auto& tag : installed) {
            ocr_language_options_.push_back({tag});
            const auto label = L"只用" + ocr_language_name(tag);
            SendMessageW(ocr_languages_, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(label.c_str()));
        }
    } else {
        ocr_language_options_.push_back({L"zh-Hans-CN", L"en-US"});
        SendMessageW(ocr_languages_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"未检测到 Windows OCR 语言包"));
    }
    ocr_upscale_ = add_control(2, 0, L"BUTTON", L"小图自动放大后识别",
                               BS_OWNERDRAW | WS_TABSTOP);
    std::wstring installed_names = L"系统已安装：";
    if (installed.empty()) {
        installed_names += L"无";
    } else {
        for (std::size_t index = 0; index < installed.size(); ++index) {
            if (index) installed_names += L"、";
            installed_names += ocr_language_name(installed[index]);
        }
    }
    const HWND installed_label = add_label(2, installed_names.c_str());
    set_font(installed_label, small_font_);
}

void SettingsWindow::create_appearance_page() {
    add_label(3, L"译文字体");
    font_family_ = add_control(3, 0, L"COMBOBOX", L"",
                               CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
                               CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP);
    for (const auto& family : installed_chinese_fonts()) {
        SendMessageW(font_family_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(family.c_str()));
    }
    add_label(3, L"强调色");
    accent_ = add_control(3, 0, L"BUTTON", L"", BS_OWNERDRAW | WS_TABSTOP, id_accent);
    add_label(3, L"关闭方式");
    // Only one close behavior is offered, so render it as a value instead of
    // a one-item drop-down.
    close_value_ = add_control(
        3, 0, L"STATIC", L"点击关闭或按ESC", SS_LEFT | SS_CENTERIMAGE, 0);
    auto_copy_ = add_control(3, 0, L"BUTTON", L"自动复制到剪贴板",
                             BS_OWNERDRAW | WS_TABSTOP);
    add_label(3, L"复制译文");
}

void SettingsWindow::create_other_page() {
    autostart_ = add_control(4, 0, L"BUTTON", L"开机自动启动",
                             BS_OWNERDRAW | WS_TABSTOP);
    add_label(4, L"配置文件");
    config_path_ = add_control(4, 0, L"EDIT", L"", ES_READONLY | ES_AUTOHSCROLL | WS_BORDER);
    open_config_ = add_control(4, 0, L"BUTTON", L"在资源管理器中打开",
                               BS_OWNERDRAW | WS_TABSTOP, id_open_config);
    restart_ = add_control(4, 0, L"BUTTON", L"重启程序",
                           BS_OWNERDRAW | WS_TABSTOP, id_restart);
    check_updates_ = add_control(4, 0, L"BUTTON", L"检查更新",
                                 BS_OWNERDRAW | WS_TABSTOP, id_check_updates);
    add_label(4, L"这里改的每一项都即时写盘。API 密钥用 Windows 的 DPAPI 加过密，只有当前这台电脑上的当前这个账户能解开——但配置文件本身仍然别随手发给别人。");
    diagnostic_command_ = add_control(
        4, 0, L"EDIT", L"ScreenTranslate.exe --selftest",
        ES_READONLY | ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP);
}

void SettingsWindow::create_text_translation_page() {
    text_target_ = add_control(
        5, 0, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
            WS_VSCROLL | WS_TABSTOP,
        id_text_target);
    populate_text_targets(text_target_);
    SendMessageW(text_target_, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
                 px(32, dpi_));
    SendMessageW(text_target_, CB_SETITEMHEIGHT, 0, px(32, dpi_));
    SendMessageW(text_target_, CB_SETMINVISIBLE, 8, 0);

    text_translate_ = add_control(5, 0, L"BUTTON", L"立即翻译",
                                  BS_OWNERDRAW | WS_TABSTOP,
                                  id_text_translate);
    text_clear_ = add_control(5, 0, L"BUTTON", L"清空",
                              BS_OWNERDRAW | WS_TABSTOP, id_text_clear);
    text_copy_ = add_control(5, 0, L"BUTTON", L"复制译文",
                             BS_OWNERDRAW | WS_TABSTOP, id_text_copy);
    text_input_ = add_control(
        5, 0, L"EDIT", L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL |
            WS_TABSTOP,
        id_text_input);
    text_output_ = add_control(
        5, 0, L"EDIT", L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL |
            WS_TABSTOP,
        id_text_output);
    SendMessageW(text_input_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(px(12, dpi_), px(12, dpi_)));
    SendMessageW(text_output_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(px(12, dpi_), px(12, dpi_)));
    SendMessageW(text_input_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"输入或粘贴需要翻译的文字"));
    SendMessageW(text_output_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"译文会显示在这里"));
}

void SettingsWindow::load_values() {
    loading_ = true;
    const auto capture_hotkey = config_.string(L"hotkey", L"Ctrl+Alt+Q");
    SetWindowTextW(capture_hotkey_, capture_hotkey.c_str());
    SetWindowTextW(toggle_hotkey_, config_.string(L"hotkey_toggle", L"Ctrl+Alt+W").c_str());
    SetWindowTextW(text_hotkey_, config_.string(
        L"hotkey_text_translate", L"Ctrl+Alt+Space").c_str());
    const auto configured_provider = config_.string(L"translator.provider", L"microsoft_free");
    const bool large_model = configured_provider == L"ai";
    int provider_position = 0;
    int provider_selection = 0;
    for (const auto& provider : providers) {
        if (provider.kind == ProviderInfo::Kind::ai) continue;
        if (provider.id == configured_provider) provider_selection = provider_position;
        ++provider_position;
    }
    loaded_provider_ = large_model ? std::wstring(L"ai") : configured_provider;
    select_combo(translator_class_, large_model ? 1 : 0);
    select_combo(provider_, large_model ? 0 : provider_selection);
    populate_profile_names();
    select_profile(config_.string(L"translator.ai_selected"));
    refresh_provider_fields(true);
    const auto zh = config_.string(L"lang.zh_target", L"en");
    int zh_index = 0;
    for (std::size_t index = 0; index < zh_targets.size(); ++index) {
        if (zh_targets[index].first == zh) zh_index = static_cast<int>(index);
    }
    select_combo(chinese_target_, zh_index);

    const auto saved_languages = config_.strings(L"ocr.languages");
    int language_index = 0;
    bool language_found = false;
    for (std::size_t index = 0; index < ocr_language_options_.size(); ++index) {
        if (ocr_language_options_[index] == saved_languages) {
            language_index = static_cast<int>(index);
            language_found = true;
            break;
        }
    }
    if (!language_found && !saved_languages.empty()) {
        std::wstring label = L"当前设置（";
        for (const auto& language : saved_languages) {
            if (label.size() > 5) label += L"、";
            label += ocr_language_name(language);
        }
        label += L"）";
        ocr_language_options_.push_back(saved_languages);
        SendMessageW(ocr_languages_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(label.c_str()));
        language_index = static_cast<int>(ocr_language_options_.size() - 1);
    }
    select_combo(ocr_languages_, language_index);
    set_checkbox(ocr_upscale_, config_.boolean(L"ocr.upscale", true));

    select_combo_text(font_family_,
                      config_.string(L"appearance.font_family", L"Microsoft YaHei UI"));
    SetWindowTextW(accent_, config_.string(L"appearance.accent", L"#28C76F").c_str());
    set_checkbox(auto_copy_, config_.boolean(L"appearance.auto_copy", true));
    set_checkbox(autostart_, autostart_enabled());
    SetWindowTextW(config_path_, config_.path().c_str());
    loading_ = false;
}

std::vector<SettingsWindow::AiProfile> SettingsWindow::read_profiles() const {
    std::vector<AiProfile> list;
    const auto stored = config_.array(L"translator.ai_profiles");
    for (const auto& entry : stored) {
        if (entry.ValueType() != JsonValueType::Object) continue;
        const auto option = entry.GetObject();
        AiProfile profile;
        profile.name = option.GetNamedString(L"name", L"").c_str();
        profile.protocol = option.GetNamedString(L"protocol", L"openai_chat").c_str();
        profile.base_url = option.GetNamedString(L"base_url", L"").c_str();
        profile.key = option.GetNamedString(L"key", L"").c_str();
        profile.model = option.GetNamedString(L"model", L"").c_str();
        if (profile.name.empty()) continue;
        list.push_back(std::move(profile));
    }
    return list;
}

void SettingsWindow::write_profiles(const std::vector<AiProfile>& list) {
    JsonArray array;
    for (const auto& profile : list) {
        JsonObject option;
        option.SetNamedValue(L"name", JsonValue::CreateStringValue(profile.name));
        option.SetNamedValue(L"protocol", JsonValue::CreateStringValue(profile.protocol));
        option.SetNamedValue(L"base_url", JsonValue::CreateStringValue(profile.base_url));
        option.SetNamedValue(L"key", JsonValue::CreateStringValue(profile.key));
        option.SetNamedValue(L"model", JsonValue::CreateStringValue(profile.model));
        array.Append(option);
    }
    config_.set_json(L"translator.ai_profiles", array);
    config_.set_string(L"translator.ai_selected", loaded_profile_);
}

int SettingsWindow::profile_index() const noexcept {
    if (loaded_profile_.empty()) return -1;
    const auto list = read_profiles();
    for (std::size_t index = 0; index < list.size(); ++index) {
        if (list[index].name == loaded_profile_) return static_cast<int>(index);
    }
    return -1;
}

std::wstring SettingsWindow::current_options_json() const {
    if (loaded_provider_ == L"ai") {
        const auto list = read_profiles();
        const int index = profile_index();
        if (index >= 0) {
            const auto& profile = list[static_cast<std::size_t>(index)];
            JsonObject option;
            option.SetNamedValue(L"name", JsonValue::CreateStringValue(profile.name));
            option.SetNamedValue(L"protocol", JsonValue::CreateStringValue(profile.protocol));
            option.SetNamedValue(L"base_url", JsonValue::CreateStringValue(profile.base_url));
            option.SetNamedValue(L"key", JsonValue::CreateStringValue(profile.key));
            option.SetNamedValue(L"model", JsonValue::CreateStringValue(profile.model));
            return option.Stringify().c_str();
        }
        return L"{}";
    }
    return config_.object(L"translator." + loaded_provider_).Stringify().c_str();
}

void SettingsWindow::populate_profile_names() {
    const auto list = read_profiles();
    SendMessageW(profile_select_, CB_RESETCONTENT, 0, 0);
    int selection = -1;
    for (std::size_t index = 0; index < list.size(); ++index) {
        SendMessageW(profile_select_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(list[index].name.c_str()));
        if (list[index].name == loaded_profile_) selection = static_cast<int>(index);
    }
    if (selection >= 0) {
        select_combo(profile_select_, selection);
    } else if (!list.empty()) {
        loaded_profile_ = list.front().name;
        select_combo(profile_select_, 0);
    } else {
        loaded_profile_.clear();
        SetWindowTextW(profile_select_, L"");
    }
    EnableWindow(profile_remove_, !loaded_profile_.empty());
}

void SettingsWindow::select_profile(std::wstring_view name) {
    const auto list = read_profiles();
    bool found = false;
    for (const auto& profile : list) {
        if (profile.name == name) {
            found = true;
            break;
        }
    }
    loaded_profile_ = found ? std::wstring(name)
                            : (list.empty() ? std::wstring() : list.front().name);
    populate_profile_names();
}

void SettingsWindow::add_profile() {
    auto list = read_profiles();
    int suffix = static_cast<int>(list.size()) + 1;
    std::wstring name = L"接口 " + std::to_wstring(suffix);
    const auto taken = [&](std::wstring_view candidate) {
        return std::any_of(list.begin(), list.end(), [&](const AiProfile& profile) {
            return profile.name == candidate;
        });
    };
    while (taken(name)) {
        ++suffix;
        name = L"接口 " + std::to_wstring(suffix);
    }
    AiProfile profile;
    profile.name = name;
    profile.protocol = L"openai_chat";
    list.push_back(profile);
    loaded_profile_ = name;
    write_profiles(list);
    populate_profile_names();
    select_combo(profile_select_, static_cast<int>(list.size() - 1));
    refresh_provider_fields(true);
    save_values(false);
}

void SettingsWindow::remove_profile() {
    auto list = read_profiles();
    if (list.empty()) return;
    const int index = std::clamp(profile_index(), 0, static_cast<int>(list.size()) - 1);
    list.erase(list.begin() + index);
    loaded_profile_ = list.empty() ? std::wstring()
                                   : list[static_cast<std::size_t>(
                                         std::min(index, static_cast<int>(list.size()) - 1))].name;
    write_profiles(list);
    populate_profile_names();
    refresh_provider_fields(false);
    save_values(false);
}

void SettingsWindow::save_profile_fields() {
    if (loaded_provider_ != L"ai") return;
    auto list = read_profiles();
    if (list.empty()) return;
    const int index = profile_index();
    if (index < 0) return;
    auto& profile = list[static_cast<std::size_t>(index)];
    auto renamed = trim(text_of(profile_select_));
    bool rename_happened = false;
    if (!renamed.empty() && renamed != profile.name) {
        // The name is the identity translator.ai_selected points at, so a rename
        // must not collide with another endpoint in the same list.
        const auto taken = [&](std::wstring_view candidate) {
            for (std::size_t other = 0; other < list.size(); ++other) {
                if (other != static_cast<std::size_t>(index) &&
                    list[other].name == candidate) {
                    return true;
                }
            }
            return false;
        };
        if (taken(renamed)) {
            const auto base = renamed;
            int suffix = 2;
            while (taken(renamed)) {
                renamed = base + L" " + std::to_wstring(suffix++);
            }
        }
        profile.name = renamed;
        rename_happened = true;
        SetWindowTextW(profile_select_, profile.name.c_str());
    }
    if (rename_happened) {
        // The combo still holds the old strings, so rebuild it in place and keep
        // the row the user just edited selected.
        SendMessageW(profile_select_, CB_RESETCONTENT, 0, 0);
        for (const auto& entry : list) {
            SendMessageW(profile_select_, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(entry.name.c_str()));
        }
        select_combo(profile_select_, index);
    }
    const int protocol = std::clamp(selected_index(ai_protocol_), 0,
                                    static_cast<int>(ai_protocols.size()) - 1);
    profile.protocol = std::wstring(ai_protocols[static_cast<std::size_t>(protocol)].first);
    profile.base_url = trim(text_of(provider_endpoint_));
    profile.key = trim(text_of(provider_key_));
    profile.model = trim(text_of(provider_model_));
    loaded_profile_ = profile.name;
    write_profiles(list);
}

void SettingsWindow::save_provider_fields() {
    if (loaded_provider_ == L"ai") {
        save_profile_fields();
        return;
    }
    if (loaded_provider_.empty()) return;
    const auto found = std::find_if(providers.begin(), providers.end(), [&](const ProviderInfo& value) {
        return value.id == loaded_provider_;
    });
    if (found == providers.end()) return;
    const auto prefix = L"translator." + loaded_provider_ + L".";
    if (found->kind != ProviderInfo::Kind::free) {
        config_.set_string(prefix + L"key", trim(text_of(provider_key_)));
    }
    if (found->kind == ProviderInfo::Kind::deepl) {
        config_.set_boolean(prefix + L"free_plan", true);
    }
}

void SettingsWindow::refresh_provider_fields(bool load) {
    const bool large_model = selected_index(translator_class_) == 1;
    if (large_model) {
        loaded_provider_ = L"ai";
    } else {
        // Same skip rule as the rest of the page: the combo only lists the
        // traditional entries, so its index counts those rows alone.
        const int chosen = selected_index(provider_);
        int seen = 0;
        loaded_provider_ = providers.front().id;
        for (const auto& provider : providers) {
            if (provider.kind == ProviderInfo::Kind::ai) continue;
            if (seen == chosen) {
                loaded_provider_ = provider.id;
                break;
            }
            ++seen;
        }
    }
    const auto traditional = std::find_if(
        providers.begin(), providers.end(),
        [&](const ProviderInfo& value) { return value.id == loaded_provider_; });
    const bool has_key = large_model ||
        (traditional != providers.end() && traditional->kind != ProviderInfo::Kind::free);
    for (const auto [control, visible] : {
             std::pair{provider_label_, !large_model},
             std::pair{provider_, !large_model},
             std::pair{profile_label_, large_model},
             std::pair{profile_select_, large_model},
             std::pair{profile_add_, large_model},
             std::pair{profile_remove_, large_model},
             std::pair{protocol_label_, large_model},
             std::pair{ai_protocol_, large_model},
             std::pair{provider_key_label_, has_key},
             std::pair{provider_key_, has_key},
             std::pair{provider_endpoint_label_, large_model},
             std::pair{provider_endpoint_, large_model},
             std::pair{provider_model_label_, large_model},
             std::pair{provider_model_, large_model},
             std::pair{model_refresh_, large_model},
             std::pair{test_button_, true},
             std::pair{test_status_, true}}) {
        ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
    }

    std::wstring_view key_cue;
    std::wstring_view endpoint_cue;
    std::wstring_view model_cue;
    if (large_model) {
        key_cue = L"sk-...";
        endpoint_cue = L"https://api.deepseek.com/v1";
        model_cue = L"点「获取模型」拉取可用模型";
    } else if (loaded_provider_ == L"deepl") {
        key_cue = L"免费版密钥以 :fx 结尾";
    }
    SendMessageW(provider_key_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(key_cue.data()));
    SendMessageW(provider_endpoint_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(endpoint_cue.data()));
    COMBOBOXINFO model_info{sizeof(model_info)};
    if (GetComboBoxInfo(provider_model_, &model_info) && model_info.hwndItem) {
        SendMessageW(model_info.hwndItem, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(model_cue.data()));
    }

    if (load || large_model) {
        if (large_model) {
            const auto list = read_profiles();
            const int index = profile_index();
            const AiProfile empty;
            const auto& profile = index >= 0 ? list[static_cast<std::size_t>(index)] : empty;
            SetWindowTextW(provider_key_, profile.key.c_str());
            SetWindowTextW(provider_endpoint_, profile.base_url.c_str());
            int protocol_index = 0;
            for (std::size_t i = 0; i < ai_protocols.size(); ++i) {
                if (ai_protocols[i].first == profile.protocol) protocol_index = static_cast<int>(i);
            }
            select_combo(ai_protocol_, protocol_index);
            SendMessageW(provider_model_, CB_RESETCONTENT, 0, 0);
            if (!profile.model.empty()) {
                SendMessageW(provider_model_, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(profile.model.c_str()));
            }
            SetWindowTextW(provider_model_, profile.model.c_str());
            clear_combo_edit_selection(provider_model_);
        } else {
            const auto prefix = L"translator." + loaded_provider_ + L".";
            SetWindowTextW(provider_key_, config_.string(prefix + L"key").c_str());
            SendMessageW(provider_model_, CB_RESETCONTENT, 0, 0);
            SetWindowTextW(provider_model_, L"");
        }
    }
    SetWindowTextW(provider_hint_,
                   large_model
                       ? L"可以添加多个大模型接口，各自填 Base URL、API Key 和模型；名称用于区分。"
                       : L"这些是免密钥或只需密钥的传统接口，由各服务商的网页/移动端接口提供，可能被限流或随服务改版失效。");
    EnableWindow(profile_remove_, large_model && !loaded_profile_.empty());
    EnableWindow(model_refresh_, large_model && !model_refreshing_);
    layout_controls();
    if (large_model && window_) {
        clear_combo_edit_selection(provider_model_);
        PostMessageW(window_, clear_combo_selection_message,
                     reinterpret_cast<WPARAM>(provider_model_), 0);
    }
}

bool SettingsWindow::save_values(bool) {
    if (loading_) return true;
    const auto old_capture = config_.string(L"hotkey", L"Ctrl+Alt+Q");
    const auto old_toggle = config_.string(L"hotkey_toggle", L"Ctrl+Alt+W");
    const auto old_text = config_.string(
        L"hotkey_text_translate", L"Ctrl+Alt+Space");
    const bool old_autostart = config_.boolean(L"autostart", false);
    bool applied_hotkeys = false;
    try {
        const auto capture = trim(text_of(capture_hotkey_));
        const auto toggle = trim(text_of(toggle_hotkey_));
        const auto text = trim(text_of(text_hotkey_));
        std::wstring error;
        const auto capture_parsed = parse_hotkey(capture, &error);
        if (!capture_parsed) throw AppError(wide_to_utf8(error));
        const auto toggle_parsed = parse_hotkey(toggle, &error);
        if (!toggle_parsed) throw AppError(wide_to_utf8(error));
        const auto text_parsed = parse_hotkey(text, &error);
        if (!text_parsed) throw AppError(wide_to_utf8(error));
        if (same_hotkey(*capture_parsed, *toggle_parsed) ||
            same_hotkey(*capture_parsed, *text_parsed) ||
            same_hotkey(*toggle_parsed, *text_parsed)) {
            throw AppError("三个快捷键不能重复");
        }
        config_.set_string(L"hotkey", capture);
        config_.set_string(L"hotkey_toggle", toggle);
        config_.set_string(L"hotkey_text_translate", text);
        const bool hotkeys_changed = capture != old_capture || toggle != old_toggle ||
                                     text != old_text;
        save_provider_fields();
        const bool large_model = selected_index(translator_class_) == 1;
        if (large_model) {
            config_.set_string(L"translator.provider", L"ai");
        } else {
            // The provider combo lists only the traditional entries, so its
            // selection is a position among the non-AI rows of the catalog.
            const int chosen = selected_index(provider_);
            int seen = 0;
            std::wstring_view selected_provider = providers.front().id;
            for (const auto& provider : providers) {
                if (provider.kind == ProviderInfo::Kind::ai) continue;
                if (seen == chosen) {
                    selected_provider = provider.id;
                    break;
                }
                ++seen;
            }
            config_.set_string(L"translator.provider", selected_provider);
        }
        config_.set_string(L"lang.zh_target",
                           zh_targets[std::clamp(selected_index(chinese_target_), 0,
                                                 static_cast<int>(zh_targets.size() - 1))].first);
        const int language_index = std::clamp(
            selected_index(ocr_languages_), 0,
            std::max(0, static_cast<int>(ocr_language_options_.size()) - 1));
        config_.set_strings(L"ocr.languages",
                            ocr_language_options_[static_cast<std::size_t>(language_index)]);
        config_.set_boolean(L"ocr.upscale", checkbox_checked(ocr_upscale_));
        config_.set_string(L"appearance.font_family", trim(text_of(font_family_)));
        const auto accent = trim(text_of(accent_));
        if (accent.size() != 7 || accent.front() != L'#') {
            throw AppError("强调色必须写成 #RRGGBB");
        }
        config_.set_string(L"appearance.accent", accent);
        config_.set_boolean(L"appearance.auto_copy", checkbox_checked(auto_copy_));
        const bool autostart = checkbox_checked(autostart_);
        if (autostart != old_autostart) set_autostart(autostart);
        config_.set_boolean(L"autostart", autostart);
        if (hotkeys_changed && hotkeys_changed_callback_) {
            if (!hotkeys_changed_callback_()) {
                config_.set_string(L"hotkey", old_capture);
                config_.set_string(L"hotkey_toggle", old_toggle);
                config_.set_string(L"hotkey_text_translate", old_text);
                SetWindowTextW(capture_hotkey_, old_capture.c_str());
                SetWindowTextW(toggle_hotkey_, old_toggle.c_str());
                SetWindowTextW(text_hotkey_, old_text.c_str());
                hotkeys_changed_callback_();
                throw AppError("快捷键未生效，可能已被其他程序占用；已恢复原设置");
            }
            applied_hotkeys = true;
        }
        config_.save();
        if (settings_changed_callback_) settings_changed_callback_();
        set_status(L"");
        return true;
    } catch (const std::exception& error) {
        if (applied_hotkeys && hotkeys_changed_callback_) {
            config_.set_string(L"hotkey", old_capture);
            config_.set_string(L"hotkey_toggle", old_toggle);
            config_.set_string(L"hotkey_text_translate", old_text);
            SetWindowTextW(capture_hotkey_, old_capture.c_str());
            SetWindowTextW(toggle_hotkey_, old_toggle.c_str());
            SetWindowTextW(text_hotkey_, old_text.c_str());
            hotkeys_changed_callback_();
        }
        const auto message = utf8_to_wide(error.what());
        set_status(message, true);
        return false;
    }
}

void SettingsWindow::start_connection_test() {
    if (test_thread_.joinable()) return;
    if (!save_values()) return;
    const auto provider = loaded_provider_;
    const std::wstring options = current_options_json();
    EnableWindow(test_button_, FALSE);
    test_pending_ = true;
    SetWindowTextW(test_status_, L"测试中…");
    const HWND target = window_;
    test_thread_ = std::jthread([target, provider, options](std::stop_token stop) noexcept {
        try {
            auto result = std::make_unique<std::pair<bool, std::wstring>>();
            try {
                WinrtApartment apartment(winrt::apartment_type::multi_threaded);
                TranslatorService service;
                const auto translated = service.translate(
                    provider, winrt::Windows::Data::Json::JsonObject::Parse(options),
                    {L"Hello"}, L"zh-Hans", std::nullopt, stop);
                result->first = translated.size() == 1 && !translated.front().empty();
                result->second = result->first ? L"连接正常" : L"接口没有返回译文";
            } catch (const std::exception& error) {
                result->first = false;
                result->second = redact_sensitive(utf8_to_wide(error.what()));
            } catch (...) {
                result->first = false;
                result->second = L"连接测试遇到未知错误";
            }
            auto* raw = result.release();
            if (!PostMessageW(target, SettingsWindow::test_completed_message, 0,
                              reinterpret_cast<LPARAM>(raw))) {
                delete raw;
            }
        } catch (...) {
            // A background failure must never terminate the tray process.
        }
    });
}

void SettingsWindow::finish_connection_test(bool success, std::wstring message) {
    if (test_thread_.joinable()) test_thread_.join();
    EnableWindow(test_button_, TRUE);
    test_pending_ = false;
    test_success_ = success;
    SetWindowTextW(test_status_, message.c_str());
    InvalidateRect(test_status_, nullptr, TRUE);
    InvalidateRect(test_button_, nullptr, FALSE);
}

void SettingsWindow::discard_connection_test_results(HWND target) noexcept {
    if (!target) target = window_;
    if (!target) return;
    MSG pending{};
    while (PeekMessageW(&pending, target, test_completed_message,
                        test_completed_message, PM_REMOVE)) {
        delete reinterpret_cast<std::pair<bool, std::wstring>*>(pending.lParam);
    }
}

void SettingsWindow::start_model_refresh() {
    if (model_thread_.joinable()) return;
    if (selected_index(translator_class_) != 1) return;
    if (!save_values()) return;

    const std::wstring provider = loaded_provider_;
    const std::wstring profile = loaded_profile_;
    const std::wstring options = current_options_json();
    const std::uint64_t generation = ++model_generation_;
    model_refreshing_ = true;
    test_pending_ = true;
    test_success_ = false;
    if (GetFocus() == model_refresh_) SetFocus(provider_model_);
    EnableWindow(model_refresh_, FALSE);
    SetWindowTextW(test_status_, L"正在拉取模型列表…");
    InvalidateRect(test_status_, nullptr, TRUE);
    InvalidateRect(model_refresh_, nullptr, FALSE);

    const HWND target = window_;
    model_thread_ = std::jthread(
        [target, generation, provider, profile,
         options](std::stop_token stop) noexcept {
            try {
                auto result = std::make_unique<ModelListResult>();
                result->generation = generation;
                result->provider = provider;
                result->profile = profile;
                try {
                    WinrtApartment apartment(winrt::apartment_type::multi_threaded);
                    TranslatorService service;
                    result->models = service.list_models(
                        winrt::Windows::Data::Json::JsonObject::Parse(options),
                        stop);
                    result->cancelled = stop.stop_requested();
                } catch (const std::exception& error) {
                    result->cancelled = stop.stop_requested();
                    if (!result->cancelled) {
                        result->error = redact_sensitive(utf8_to_wide(error.what()));
                    }
                } catch (...) {
                    result->cancelled = stop.stop_requested();
                    if (!result->cancelled) {
                        result->error = L"拉取模型列表时遇到未知错误";
                    }
                }
                auto* raw = result.release();
                if (!PostMessageW(target, SettingsWindow::models_completed_message,
                                  0, reinterpret_cast<LPARAM>(raw))) {
                    delete raw;
                }
            } catch (...) {
                // Background failures must not escape into the tray process.
            }
        });
}

void SettingsWindow::finish_model_refresh(ModelListResult result) {
    if (model_thread_.joinable()) model_thread_.join();
    if (result.generation != model_generation_ ||
        result.profile != loaded_profile_) {
        return;
    }
    model_refreshing_ = false;
    test_pending_ = false;
    const bool model_provider = selected_index(translator_class_) == 1;
    EnableWindow(model_refresh_, model_provider);
    InvalidateRect(model_refresh_, nullptr, FALSE);
    if (result.cancelled) return;

    if (!result.error.empty() || result.models.empty()) {
        test_success_ = false;
        auto message = result.error.empty()
            ? std::wstring(L"接口没有返回任何模型")
            : std::move(result.error);
        if (message.size() > 160) message.resize(160);
        SetWindowTextW(test_status_, message.c_str());
        InvalidateRect(test_status_, nullptr, TRUE);
        return;
    }

    const auto keep = trim(text_of(provider_model_));
    SendMessageW(provider_model_, CB_RESETCONTENT, 0, 0);
    for (const auto& model : result.models) {
        SendMessageW(provider_model_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(model.c_str()));
    }
    const auto found = std::find(result.models.begin(), result.models.end(), keep);
    const bool kept = found != result.models.end();
    const int selected = kept
        ? static_cast<int>(found - result.models.begin())
        : 0;
    SendMessageW(provider_model_, CB_SETCURSEL, selected, 0);
    clear_combo_edit_selection(provider_model_);
    PostMessageW(window_, clear_combo_selection_message,
                 reinterpret_cast<WPARAM>(provider_model_), 0);

    test_success_ = true;
    std::wstring message = L"拉到 " + std::to_wstring(result.models.size()) +
                           L" 个模型";
    if (!kept) message += L"，已选 " + text_of(provider_model_);
    SetWindowTextW(test_status_, message.c_str());
    InvalidateRect(test_status_, nullptr, TRUE);
    save_values(false);
}

void SettingsWindow::cancel_model_refresh(bool clear_status) noexcept {
    const bool was_refreshing = model_refreshing_;
    ++model_generation_;
    if (model_thread_.joinable()) {
        model_thread_.request_stop();
        model_thread_.join();
    }
    discard_model_refresh_results();
    model_refreshing_ = false;
    if (was_refreshing) test_pending_ = false;
    if (model_refresh_ && IsWindow(model_refresh_)) {
        EnableWindow(model_refresh_, TRUE);
        InvalidateRect(model_refresh_, nullptr, FALSE);
    }
    if (clear_status && test_status_ && IsWindow(test_status_)) {
        test_success_ = false;
        SetWindowTextW(test_status_, L"");
        InvalidateRect(test_status_, nullptr, TRUE);
    }
}

void SettingsWindow::discard_model_refresh_results(HWND target) noexcept {
    if (!target) target = window_;
    if (!target) return;
    MSG pending{};
    while (PeekMessageW(&pending, target, models_completed_message,
                        models_completed_message, PM_REMOVE)) {
        delete reinterpret_cast<ModelListResult*>(pending.lParam);
    }
}

void SettingsWindow::set_status(std::wstring_view message, bool error) {
    status_error_ = error;
    SetWindowTextW(status_, std::wstring(message).c_str());
    ShowWindow(status_, message.empty() ? SW_HIDE : SW_SHOWNA);
    if (!message.empty()) InvalidateRect(status_, nullptr, TRUE);
}

void SettingsWindow::show_page(int index) {
    if (!window_) return;
    {
        ScopedRedrawPause redraw_pause(window_);
        current_page_ = std::clamp(
            index, 0, static_cast<int>(page_controls_.size() - 1));
        for (std::size_t page = 0; page < page_controls_.size(); ++page) {
            for (const auto control : page_controls_[page]) {
                ShowWindow(control, static_cast<int>(page) == current_page_ ? SW_SHOW : SW_HIDE);
            }
        }
        if (current_page_ == 1) refresh_provider_fields(false);
        if (current_page_ == 5) refresh_text_translation();
        layout_controls();
        clear_combo_edit_selection(provider_model_);
        const auto navigation_index = navigation_index_for_page(current_page_);
        if (navigation_[navigation_index]) {
            SetFocus(navigation_[navigation_index]);
        }
        for (const auto button : navigation_) InvalidateRect(button, nullptr, FALSE);
    }
    RedrawWindow(window_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW |
                     RDW_FRAME | RDW_NOERASE);
}

void SettingsWindow::layout_controls() {
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right;
    const int height = client.bottom;
    const int sidebar = px(logical_sidebar_width, dpi_);
    for (std::size_t index = 0; index < navigation_.size(); ++index) {
        MoveWindow(navigation_[index], px(12, dpi_), px(18 + static_cast<int>(index) * 42, dpi_),
                   std::max(1, sidebar - px(24, dpi_)), px(38, dpi_), FALSE);
    }
    MoveWindow(status_, sidebar + px(logical_page_margin, dpi_), height - px(31, dpi_),
               std::max(1, width - sidebar - px(logical_page_margin * 2, dpi_)),
               px(20, dpi_), FALSE);

    const int card_left = sidebar + px(logical_page_margin, dpi_);
    const int card_right = width - px(logical_page_margin, dpi_);
    const int inner_left = card_left + px(18, dpi_);
    const int inner_right = card_right - px(18, dpi_);
    const int available_width = std::max(1, inner_right - inner_left);
    const int label_gap = px(logical_label_gap, dpi_);
    // Size the caption column from the labels actually shown on this page.
    // A fixed, generous column made short captions float towards the middle of
    // the card and left a large blank gutter on the left.
    std::vector<HWND> labels;
    const auto collect_label = [&](HWND label) {
        if (!label ||
            (GetWindowLongPtrW(label, GWL_STYLE) & WS_VISIBLE) == 0) {
            return;
        }
        labels.push_back(label);
    };
    if (current_page_ == 0 && page_controls_[0].size() >= 5) {
        collect_label(page_controls_[0][0]);
        collect_label(page_controls_[0][2]);
        collect_label(page_controls_[0][4]);
    } else if (current_page_ == 1) {
        const auto& controls = page_controls_[1];
        for (const std::size_t index : {0U, 2U, 4U, 8U, 10U, 12U, 14U, 20U}) {
            if (index < controls.size()) collect_label(controls[index]);
        }
    } else if (current_page_ == 2) {
        const auto& controls = page_controls_[2];
        if (controls.size() > 3) {
            collect_label(controls[0]);
            collect_label(controls[3]);
        }
    } else if (current_page_ == 3) {
        const auto& controls = page_controls_[3];
        for (const std::size_t index : {0U, 2U, 4U, 7U}) {
            if (index < controls.size()) collect_label(controls[index]);
        }
    }
    int label_text_width = px(logical_field_min_offset, dpi_) - label_gap;
    if (!labels.empty()) {
        HDC dc = GetDC(window_);
        if (dc) {
            const HGDIOBJ previous = SelectObject(dc, font_);
            for (const HWND label : labels) {
                const std::wstring caption = text_of(label);
                SIZE measured{};
                GetTextExtentPoint32W(dc, caption.c_str(),
                                      static_cast<int>(caption.size()), &measured);
                label_text_width = std::max(label_text_width,
                                            static_cast<int>(measured.cx));
            }
            SelectObject(dc, previous);
            ReleaseDC(window_, dc);
        }
    }
    field_offset_ = std::clamp(
        label_text_width + label_gap,
        px(logical_field_min_offset, dpi_),
        px(logical_field_max_offset, dpi_));
    const int label_width = std::max(1, field_offset_ - label_gap);
    const int field_left = inner_left + field_offset_;
    const int field_width = std::max(1, inner_right - field_left);
    const int control_height = px(logical_control_height, dpi_);

    const auto move_field = [&](HWND field, int logical_top, int logical_width = -1) {
        if (!field) return;
        wchar_t type[24]{};
        GetClassNameW(field, type, static_cast<int>(std::size(type)));
        const bool combo = _wcsicmp(type, L"ComboBox") == 0;
        const bool edit = _wcsicmp(type, L"Edit") == 0;
        const int requested = logical_width < 0 ? field_width : px(logical_width, dpi_);
        const int field_top = px(logical_top + 13, dpi_);
        const int inset_x = edit
            ? std::max(1, px(logical_edit_inset_x, dpi_)) : 0;
        const int inset_top = edit
            ? std::max(1, px(logical_edit_inset_top, dpi_)) : 0;
        const int inset_bottom = edit
            ? std::max(1, px(logical_edit_inset_bottom, dpi_)) : 0;
        MoveWindow(field, field_left + inset_x, field_top + inset_top,
                   std::max(1, std::min(field_width, requested) - inset_x * 2),
                   combo ? px(260, dpi_)
                         : std::max(1, control_height - inset_top - inset_bottom), FALSE);
    };
    const auto place = [&](HWND label, HWND field, int logical_top, int logical_width = -1) {
        if (!label || !field) return;
        const auto label_style = GetWindowLongPtrW(label, GWL_STYLE);
        SetWindowLongPtrW(label, GWL_STYLE,
                          (label_style & ~static_cast<LONG_PTR>(SS_TYPEMASK)) |
                          SS_LEFT | SS_CENTERIMAGE);
        const int label_top = px(logical_top + 13, dpi_);
        MoveWindow(label, inner_left, label_top, label_width,
                   control_height, FALSE);
        move_field(field, logical_top, logical_width);
    };
    const auto place_hint = [&](HWND hint, int logical_y, int logical_height,
                                bool full_width = true) {
        if (!hint) return;
        const auto style = GetWindowLongPtrW(hint, GWL_STYLE);
        SetWindowLongPtrW(hint, GWL_STYLE,
                          (style & ~static_cast<LONG_PTR>(SS_TYPEMASK)) | SS_LEFT);
        set_font(hint, small_font_);
        MoveWindow(hint, inner_left, px(logical_y, dpi_),
                   full_width ? available_width : label_width,
                   px(logical_height, dpi_), FALSE);
    };

    if (current_page_ == 0) {
        auto& c = page_controls_[0];
        if (c.size() >= 7) {
            const auto place_hotkey = [&](HWND label, HWND edit, int top) {
                const auto label_style = GetWindowLongPtrW(label, GWL_STYLE);
                SetWindowLongPtrW(label, GWL_STYLE,
                                  (label_style & ~static_cast<LONG_PTR>(SS_TYPEMASK)) |
                                      SS_LEFT | SS_CENTERIMAGE);
                MoveWindow(label, inner_left, px(top, dpi_), label_width,
                           px(34, dpi_), FALSE);
                const int inset_x = std::max(
                    1, px(logical_edit_inset_x, dpi_));
                const int inset_top = std::max(
                    1, px(logical_edit_inset_top, dpi_));
                const int inset_bottom = std::max(
                    1, px(logical_edit_inset_bottom, dpi_));
                MoveWindow(edit, field_left + inset_x,
                           px(top, dpi_) + inset_top,
                           std::max(1, field_width - inset_x * 2),
                           px(34, dpi_) - inset_top - inset_bottom, FALSE);
            };
            place_hotkey(c[0], capture_hotkey_, 103);
            place_hotkey(c[2], toggle_hotkey_, 180);
            place_hotkey(c[4], text_hotkey_, 257);
            MoveWindow(c[6], 0, 0, 0, 0, FALSE);
        }
    } else if (current_page_ == 1) {
        auto& c = page_controls_[1];
        const bool large_model = selected_index(translator_class_) == 1;
        int y = logical_content_top;
        if (!c.empty()) place(c[0], translator_class_, y);
        y += logical_setting_row_height;
        if (large_model) {
            place(c[4], profile_select_, y);
            const int gap = px(6, dpi_);
            const int button_width = px(56, dpi_);
            const int select_width = std::max(
                px(110, dpi_),
                field_width - gap * 2 - button_width * 2);
            MoveWindow(profile_select_, field_left, px(y + 13, dpi_),
                       select_width, px(260, dpi_), FALSE);
            MoveWindow(profile_add_, field_left + select_width + gap,
                       px(y + 13, dpi_), button_width, control_height, FALSE);
            MoveWindow(profile_remove_,
                       field_left + select_width + gap * 2 + button_width,
                       px(y + 13, dpi_), button_width, control_height, FALSE);
            y += logical_setting_row_height;
            place(c[8], ai_protocol_, y);
            y += logical_setting_row_height;
        } else {
            place(c[2], provider_, y);
            y += logical_setting_row_height;
        }
        const auto place_row = [&](HWND label, HWND field, int top) {
            place(label, field, top);
            y = top + logical_setting_row_height;
        };
        const auto chosen = std::find_if(
            providers.begin(), providers.end(),
            [&](const ProviderInfo& value) { return value.id == loaded_provider_; });
        const bool has_key = large_model ||
            (chosen != providers.end() && chosen->kind != ProviderInfo::Kind::free);
        if (has_key) place_row(provider_key_label_, provider_key_, y);
        if (large_model) {
            place_row(provider_endpoint_label_, provider_endpoint_, y);
            place_row(provider_model_label_, provider_model_, y);
        }
        // 「获取模型」sits on its own row next to 「测试连接」 so that the model
        // combo keeps the full width of the field column.
        const int gap = px(6, dpi_);
        const int button_width = px(84, dpi_);
        if (large_model) {
            MoveWindow(model_refresh_, field_left, px(y + 13, dpi_),
                       button_width, control_height, FALSE);
            MoveWindow(test_button_, field_left + button_width + gap,
                       px(y + 13, dpi_), button_width, control_height, FALSE);
            MoveWindow(test_status_, field_left + (button_width + gap) * 2,
                       px(y + 13, dpi_),
                       std::max(1, inner_right - field_left -
                                       (button_width + gap) * 2),
                       control_height, FALSE);
        } else {
            MoveWindow(test_button_, field_left, px(y + 13, dpi_),
                       button_width, control_height, FALSE);
            MoveWindow(test_status_, field_left + button_width + gap,
                       px(y + 13, dpi_),
                       std::max(1, inner_right - field_left - button_width - gap),
                       control_height, FALSE);
        }
        y += logical_setting_row_height;
        // The last control sits 13px into its row and is 34px tall, so the card
        // has to reach a little past the row band before it can close below the
        // buttons. paint() recomputes the same figure from the row count.
        const int main_bottom = y + 8;
        MoveWindow(provider_hint_, field_left, px(main_bottom + 6, dpi_),
                   field_width, px(34, dpi_), FALSE);
        const int language_top = main_bottom + 52;
        place(c[20], chinese_target_, language_top + 47);
    } else if (current_page_ == 2) {
        auto& c = page_controls_[2];
        if (c.size() >= 7) {
            place(c[0], ocr_engine_value_, 90);
            MoveWindow(ocr_engine_note_, field_left, px(141, dpi_),
                       field_width, px(30, dpi_), FALSE);
            place(c[3], ocr_languages_, 165);
            MoveWindow(c[6], field_left, px(221, dpi_), field_width,
                       px(24, dpi_), FALSE);
            MoveWindow(ocr_upscale_, field_left, px(253, dpi_), px(230, dpi_),
                       control_height, FALSE);
        }
    } else if (current_page_ == 3) {
        auto& c = page_controls_[3];
        if (c.size() >= 8) {
            place(c[0], font_family_, 90);
            place(c[2], accent_, 163);
            place(c[4], close_value_, 250);
            place(c[7], auto_copy_, 293);
        }
    } else if (current_page_ == 4) {
        auto& c = page_controls_[4];
        if (c.size() >= 8) {
            MoveWindow(autostart_, inner_left, px(103, dpi_),
                       px(230, dpi_), control_height, FALSE);
            MoveWindow(restart_, inner_left, px(167, dpi_),
                       px(84, dpi_), control_height, FALSE);
            MoveWindow(check_updates_, inner_left + px(84, dpi_) + px(10, dpi_),
                       px(167, dpi_), px(84, dpi_), control_height, FALSE);
            MoveWindow(c[1], 0, 0, 0, 0, FALSE);

            const int inset_x = std::max(
                1, px(logical_edit_inset_x, dpi_));
            const int inset_top = std::max(
                1, px(logical_edit_inset_top, dpi_));
            const int inset_bottom = std::max(
                1, px(logical_edit_inset_bottom, dpi_));
            MoveWindow(config_path_, inner_left + inset_x, px(296, dpi_) + inset_top,
                       std::max(1, available_width - inset_x * 2),
                       std::max(1, px(30, dpi_) - inset_top - inset_bottom), FALSE);
            MoveWindow(open_config_, inner_left, px(329, dpi_),
                       px(149, dpi_), control_height, FALSE);
            place_hint(c[6], 368, 38);

            MoveWindow(diagnostic_command_, inner_left + inset_x,
                       px(503, dpi_) + inset_top,
                       std::max(1, available_width - inset_x * 2),
                       std::max(1, px(34, dpi_) - inset_top - inset_bottom), FALSE);
        }
    } else if (current_page_ == 5) {
        const int toolbar_top = px(90, dpi_);
        const int target_left = inner_left + px(68, dpi_);
        MoveWindow(text_target_, target_left, toolbar_top, px(166, dpi_),
                   px(318, dpi_), FALSE);

        int right = inner_right;
        MoveWindow(text_translate_, right - px(82, dpi_), toolbar_top,
                   px(82, dpi_), control_height, FALSE);
        right -= px(90, dpi_);
        MoveWindow(text_copy_, right - px(76, dpi_), toolbar_top,
                   px(76, dpi_), control_height, FALSE);
        right -= px(84, dpi_);
        MoveWindow(text_clear_, right - px(54, dpi_), toolbar_top,
                   px(54, dpi_), control_height, FALSE);

        MoveWindow(text_input_, card_left + px(13, dpi_), px(166, dpi_),
                   std::max(1, card_right - card_left - px(26, dpi_)),
                   px(118, dpi_), FALSE);
        MoveWindow(text_output_, card_left + px(13, dpi_), px(345, dpi_),
                   std::max(1, card_right - card_left - px(26, dpi_)),
                   px(143, dpi_), FALSE);
    }
}

void SettingsWindow::paint() {
    wchar_t accent_text[16]{L'#', L'2', L'8', L'C', L'7', L'6', L'F', L'\0'};
    if (accent_) GetWindowTextW(accent_, accent_text, static_cast<int>(std::size(accent_text)));
    const COLORREF accent = parse_rgb_color(accent_text);
    PAINTSTRUCT state{};
    HDC target = BeginPaint(window_, &state);
    if (!target) {
        EndPaint(window_, &state);
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    if (client.right <= client.left || client.bottom <= client.top) {
        EndPaint(window_, &state);
        return;
    }
    HDC dc = CreateCompatibleDC(target);
    if (!dc) {
        FillRect(target, &client, background_);
        EndPaint(window_, &state);
        return;
    }
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    if (!bitmap) {
        FillRect(target, &client, background_);
        DeleteDC(dc);
        EndPaint(window_, &state);
        return;
    }
    const auto old_bitmap = SelectObject(dc, bitmap);
    FillRect(dc, &client, background_);

    const int sidebar = px(logical_sidebar_width, dpi_);
    RECT side{0, 0, sidebar, client.bottom};
    FillRect(dc, &side, sidebar_background_);
    HPEN divider = CreatePen(PS_SOLID, 1, color_line);
    const auto old_pen = SelectObject(dc, divider);
    MoveToEx(dc, sidebar - 1, 0, nullptr);
    LineTo(dc, sidebar - 1, client.bottom);
    SelectObject(dc, old_pen);
    DeleteObject(divider);

    const int card_left = sidebar + px(logical_page_margin, dpi_);
    const int card_right = client.right - px(logical_page_margin, dpi_);
    const int inner_left = card_left + px(18, dpi_);
    const int inner_right = card_right - px(18, dpi_);
    const int field_offset = field_offset_ > 0
        ? field_offset_
        : px(logical_field_min_offset, dpi_);
    const int field_left = inner_left + field_offset;
    const int text_right = field_left - px(logical_label_gap, dpi_);
    draw_text(dc, page_names[static_cast<std::size_t>(current_page_)],
              RECT{card_left, px(20, dpi_), card_right, px(48, dpi_)},
              title_font_, RGB(242, 243, 246), DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_text(dc, page_subtitles[static_cast<std::size_t>(current_page_)],
              RECT{card_left, px(50, dpi_), card_right, px(72, dpi_)},
              small_font_, color_text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const auto card = [&](int top, int bottom) {
        fill_round_rect(dc, RECT{card_left, px(top, dpi_), card_right, px(bottom, dpi_)},
                        px(7, dpi_), color_card, color_line);
    };
    const auto separator = [&](int logical_y) {
        HPEN pen = CreatePen(PS_SOLID, 1, color_line);
        const auto previous = SelectObject(dc, pen);
        MoveToEx(dc, inner_left, px(logical_y, dpi_), nullptr);
        LineTo(dc, inner_right, px(logical_y, dpi_));
        SelectObject(dc, previous);
        DeleteObject(pen);
    };
    const auto group = [&](int top, int rows) {
        const int bottom = top + rows * logical_setting_row_height;
        card(top, bottom);
        for (int row = 1; row < rows; ++row) {
            separator(top + row * logical_setting_row_height);
        }
        return bottom;
    };
    const auto section = [&](std::wstring_view text, int top) {
        draw_text(dc, text,
                  RECT{card_left + px(2, dpi_), px(top, dpi_),
                       card_right, px(top + 24, dpi_)},
                  small_font_, color_text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    };
    // Key/description lists get a column sized to their longest key instead of
    // a fixed 132px block, so short keys do not leave a wide empty gap.
    const auto key_column_width = [&](const auto& rows) {
        const HGDIOBJ previous = SelectObject(dc, small_font_);
        int width = 0;
        for (const auto& row : rows) {
            SIZE measured{};
            GetTextExtentPoint32W(dc, row.first.data(),
                                  static_cast<int>(row.first.size()), &measured);
            width = std::max(width, static_cast<int>(measured.cx));
        }
        SelectObject(dc, previous);
        return std::clamp(width + px(14, dpi_),
                          px(84, dpi_), px(180, dpi_));
    };
    const auto draw_key_rows = [&](const auto& rows, int top, int step) {
        const int keys_width = key_column_width(rows);
        int y = top;
        for (const auto& row : rows) {
            draw_text(dc, row.first,
                      RECT{inner_left, px(y, dpi_),
                           inner_left + keys_width, px(y + step, dpi_)},
                      small_font_, color_text,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            draw_text(dc, row.second,
                      RECT{inner_left + keys_width + px(8, dpi_), px(y, dpi_),
                           inner_right, px(y + step, dpi_)},
                      small_font_, color_text_dim,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += step;
        }
    };
    const auto row_copy = [&](std::wstring_view title, std::wstring_view description,
                              int top, bool paint_title) {
        if (paint_title && !title.empty()) {
            draw_text(dc, title,
                      RECT{inner_left, px(top + 7, dpi_), text_right,
                           px(top + 29, dpi_)},
                      font_, color_text, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                                               DT_END_ELLIPSIS);
        }
        if (!description.empty()) {
            draw_text(dc, description,
                      RECT{inner_left, px(top + 30, dpi_), text_right,
                           px(top + 52, dpi_)},
                      small_font_, color_text_dim, DT_LEFT | DT_VCENTER |
                                                   DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    };
    if (current_page_ == 0) {
        card(90, 341);
        draw_text(dc, L"按下后拉框，松手立刻翻译",
                  RECT{field_left, px(143, dpi_), inner_right,
                       px(162, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, L"同一个键来回切：译文开着就收进托盘，收着就叫回来",
                  RECT{field_left, px(220, dpi_), inner_right,
                       px(239, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, L"弹出聚焦式输入框；Esc 或再次按快捷键隐藏",
                  RECT{field_left, px(297, dpi_), inner_right,
                       px(316, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        section(L"框选时", 361);
        card(387, 448);
        const std::array<std::pair<std::wstring_view, std::wstring_view>, 2>
            selection_rows{{
                {L"拖动鼠标", L"画出要翻译的区域，松手立刻识别并翻译"},
                {L"Esc / 右键", L"取消这次框选"},
            }};
        draw_key_rows(selection_rows, 400, 18);
    } else if (current_page_ == 1) {
        const bool large_model = selected_index(translator_class_) == 1;
        const auto chosen = std::find_if(
            providers.begin(), providers.end(),
            [&](const ProviderInfo& value) { return value.id == loaded_provider_; });
        const bool has_key = large_model ||
            (chosen != providers.end() && chosen->kind != ProviderInfo::Kind::free);
        // Mirror the row arithmetic in layout_controls so the card hugs the
        // fields: one row per selection block, then the credential rows and the
        // button row. The extra 8px covers the bottom of the button controls,
        // which start 13px into their row and are 34px tall.
        int rows = 2;  // class row + provider/profile block
        if (large_model) rows += 1;  // the profile block spans two rows
        if (has_key) rows += 1;
        if (large_model) rows += 2;  // base URL + model
        rows += 1;                   // 获取模型 / 测试连接
        const int main_bottom = logical_content_top +
                                rows * logical_setting_row_height + 8;
        card(90, main_bottom);
        const int section_y = main_bottom + 52;
        section(L"语言", section_y);
        card(section_y + 26, section_y + 112);
    } else if (current_page_ == 2) {
        card(90, 300);
        section(L"装其他语言", 320);
        card(346, 430);
        draw_text(dc,
                  L"需要日语、韩语等语言时，到 Windows 设置添加该语言，再进入语言选项安装光学字符识别，装好后重启本程序即可。",
                  RECT{inner_left, px(358, dpi_), inner_right,
                       px(392, dpi_)},
                  small_font_, color_text_dim, DT_LEFT | DT_TOP | DT_WORDBREAK);
    } else if (current_page_ == 3) {
        const int main_bottom = 354;
        card(90, main_bottom);
        draw_text(dc,
                  L"只列出能显示中文的字体——没有汉字的字体会让译文退化成系统替换字，很难看。",
                  RECT{field_left, px(143, dpi_), inner_right, px(166, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        draw_text(dc,
                  L"框选边框、八个拖拽手柄、进度条和菜单高亮都用这个颜色。改完立刻生效；已经开着的译文窗口保持原色，下一个才换。",
                  RECT{field_left, px(210, dpi_), inner_right, px(239, dpi_)},
                  small_font_, color_text_dim, DT_LEFT | DT_TOP | DT_WORDBREAK);
        separator(247);

    } else if (current_page_ == 4) {
        card(90, 238);
        separator(156);
        draw_text(dc, L"装完之后每次开机自动到托盘待命，不会弹窗口。",
                  RECT{inner_left, px(132, dpi_), inner_right, px(153, dpi_)},
                  small_font_, color_text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, L"刚给 Windows 装完新的识别语言包，重启一下才会生效。",
                  RECT{inner_left, px(205, dpi_), inner_right, px(229, dpi_)},
                  small_font_, color_text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        section(L"配置文件", 258);
        card(283, 405);

        section(L"出问题时", 427);
        card(452, 550);
        draw_text(dc,
                  L"命令行跑一次自检，会逐项检查 OCR 语言包 → 文字识别 → 译文排版 → 翻译接口 → 全局快捷键，一眼看出断在哪一环：",
                  RECT{inner_left, px(466, dpi_), inner_right, px(497, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);
    } else if (current_page_ == 5) {
        card(82, 132);
        draw_text(dc, L"目标语言",
                  RECT{inner_left, px(90, dpi_), inner_left + px(62, dpi_),
                       px(124, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const RECT input_panel{card_left, px(144, dpi_), card_right,
                               px(299, dpi_)};
        const RECT output_panel{card_left, px(311, dpi_), card_right,
                                px(503, dpi_)};
        fill_round_rect(dc, input_panel, px(7, dpi_), color_input,
                        GetFocus() == text_input_ ? accent : color_line);
        fill_round_rect(dc, output_panel, px(7, dpi_), color_input,
                        GetFocus() == text_output_ ? accent : color_line);
        draw_text(dc, L"原文",
                  RECT{input_panel.left + px(13, dpi_), input_panel.top,
                       input_panel.left + px(80, dpi_), input_panel.top + px(30, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, L"译文",
                  RECT{output_panel.left + px(13, dpi_), output_panel.top,
                       output_panel.left + px(80, dpi_), output_panel.top + px(30, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (text_session_) {
            const auto counter = std::to_wstring(text_session_->character_count()) +
                                 L" / 5000";
            draw_text(dc, counter,
                      RECT{input_panel.right - px(120, dpi_), input_panel.top,
                           input_panel.right - px(13, dpi_), input_panel.top + px(30, dpi_)},
                      small_font_,
                      text_session_->state() == TextTranslationState::too_long
                          ? color_bad : color_text_faint,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            const bool error = text_session_->state() == TextTranslationState::error ||
                               text_session_->state() == TextTranslationState::too_long;
            draw_text(dc, text_translation_status(*text_session_),
                      RECT{card_left + px(2, dpi_), px(513, dpi_),
                           card_right, px(537, dpi_)},
                      small_font_, error ? color_bad : color_text_dim,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        draw_text(dc, L"自动选择会把中文译成设置的目标语言，其他语言译成简体中文。",
                  RECT{card_left + px(2, dpi_), px(537, dpi_),
                       card_right, px(558, dpi_)},
                  small_font_, color_text_faint,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    const HWND focused = GetFocus();
    for (const auto control : page_controls_[static_cast<std::size_t>(current_page_)]) {
        if (!control || !IsWindowVisible(control)) continue;
        wchar_t type[24]{};
        GetClassNameW(control, type, static_cast<int>(std::size(type)));
        if (_wcsicmp(type, L"Edit") != 0 || control == text_input_ ||
            control == text_output_) {
            continue;
        }
        RECT frame{};
        GetWindowRect(control, &frame);
        MapWindowPoints(HWND_DESKTOP, window_, reinterpret_cast<POINT*>(&frame), 2);
        const int inset_x = std::max(1, px(logical_edit_inset_x, dpi_));
        const int inset_top = std::max(1, px(logical_edit_inset_top, dpi_));
        const int inset_bottom = std::max(1, px(logical_edit_inset_bottom, dpi_));
        frame.left -= inset_x;
        frame.right += inset_x;
        frame.top -= inset_top;
        frame.bottom += inset_bottom;
        fill_round_rect(dc, frame, px(logical_edit_corner_radius, dpi_), color_input,
                        focused == control ? accent : color_line);
    }

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window_, &state);
}

LRESULT SettingsWindow::draw_item(const DRAWITEMSTRUCT& item) {
    HDC dc = item.hDC;
    RECT rect = item.rcItem;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hot = (item.itemState & ODS_HOTLIGHT) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const COLORREF accent = parse_rgb_color(accent_ ? text_of(accent_) :
                                         config_.string(L"appearance.accent", L"#28C76F"));

    if (item.CtlType == ODT_COMBOBOX) {
        const bool list_item = (item.itemState & ODS_COMBOBOXEDIT) == 0;
        const bool selected = list_item && (item.itemState & ODS_SELECTED) != 0;
        FillRect(dc, &rect, input_background_);
        if (selected) {
            RECT highlight = rect;
            InflateRect(&highlight, -px(4, dpi_), -px(2, dpi_));
            const COLORREF selected_fill = blend_color(color_input, accent, 16);
            const COLORREF selected_border = blend_color(color_line_high, accent, 34);
            fill_round_rect(dc, highlight, px(5, dpi_), selected_fill, selected_border);
            RECT marker{highlight.left + px(3, dpi_), highlight.top + px(6, dpi_),
                        highlight.left + px(5, dpi_), highlight.bottom - px(6, dpi_)};
            HBRUSH marker_brush = CreateSolidBrush(accent);
            FillRect(dc, &marker, marker_brush);
            DeleteObject(marker_brush);
        }
        std::wstring value;
        if (item.itemID != static_cast<UINT>(-1)) {
            const LRESULT length = SendMessageW(item.hwndItem, CB_GETLBTEXTLEN, item.itemID, 0);
            if (length >= 0) {
                value.resize(static_cast<std::size_t>(length) + 1);
                SendMessageW(item.hwndItem, CB_GETLBTEXT, item.itemID,
                             reinterpret_cast<LPARAM>(value.data()));
                value.resize(static_cast<std::size_t>(length));
            }
        } else {
            value = text_of(item.hwndItem);
        }
        rect.left += px(list_item ? 13 : 9, dpi_);
        rect.right -= px(list_item ? 9 : 25, dpi_);
        if (!list_item) {
            OffsetRect(&rect, 0, px(logical_field_text_offset_y, dpi_));
        }
        draw_text(dc, value, rect, font_, disabled ? color_text_faint
                                                    : selected ? RGB(244, 247, 245)
                                                               : color_text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (!list_item) {
            HPEN pen = CreatePen(PS_SOLID, px(2, dpi_), color_text_dim);
            const auto previous = SelectObject(dc, pen);
            const int x = item.rcItem.right - px(14, dpi_);
            const int y = (item.rcItem.top + item.rcItem.bottom) / 2;
            MoveToEx(dc, x - px(4, dpi_), y - px(2, dpi_), nullptr);
            LineTo(dc, x, y + px(2, dpi_));
            LineTo(dc, x + px(4, dpi_), y - px(2, dpi_));
            SelectObject(dc, previous);
            DeleteObject(pen);
        }
        return TRUE;
    }

    if (item.CtlType != ODT_BUTTON) return FALSE;
    if (item.CtlID >= id_nav_first && item.CtlID <= id_nav_last) {
        const int navigation_index = static_cast<int>(item.CtlID) - id_nav_first;
        const int page = navigation_page_order[static_cast<std::size_t>(navigation_index)];
        const bool active = page == current_page_;
        const COLORREF fill = pressed ? RGB(35, 39, 47)
                              : active ? RGB(35, 39, 47)
                              : hot ? RGB(27, 30, 37) : color_sidebar;
        FillRect(dc, &rect, sidebar_background_);
        fill_round_rect(dc, rect, px(6, dpi_), fill, fill);
        const COLORREF icon_color = active ? accent : RGB(151, 158, 169);
        const COLORREF text_color = active ? RGB(244, 245, 247) : RGB(166, 172, 182);
        const int icon_size = px(18, dpi_);
        const int icon_top = rect.top + (rect.bottom - rect.top - icon_size) / 2;
        RECT icon_rect{rect.left + px(13, dpi_), icon_top,
                       rect.left + px(13, dpi_) + icon_size, icon_top + icon_size};
        draw_settings_nav_icon(dc, page, icon_rect, icon_color, icon_font_);
        RECT text_rect{rect.left + px(43, dpi_), rect.top, rect.right - px(8, dpi_), rect.bottom};
        draw_text(dc, page_names[static_cast<std::size_t>(page)], text_rect, font_, text_color,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }

    if (item.hwndItem == accent_) {
        FillRect(dc, &rect, card_background_);
        const auto selected = text_of(accent_);
        const int radius = px(9, dpi_);
        const int selected_radius = px(11, dpi_);
        const int inner_radius = px(7, dpi_);
        const int center_y = (rect.top + rect.bottom) / 2;
        for (std::size_t index = 0; index < accent_swatches.size(); ++index) {
            const int center_x = rect.left + px(12 + static_cast<int>(index) * 30, dpi_);
            const auto& [name, color] = accent_swatches[index];
            const bool active = _wcsicmp(selected.c_str(), std::wstring(name).c_str()) == 0;
            if (active) {
                HPEN ring = CreatePen(PS_SOLID, std::max(1, px(2, dpi_)), color);
                const auto old_pen = SelectObject(dc, ring);
                const auto old_brush = SelectObject(dc, GetStockObject(NULL_BRUSH));
                Ellipse(dc, center_x - selected_radius, center_y - selected_radius,
                        center_x + selected_radius, center_y + selected_radius);
                SelectObject(dc, old_brush);
                SelectObject(dc, old_pen);
                DeleteObject(ring);
                HBRUSH brush = CreateSolidBrush(color);
                const auto previous = SelectObject(dc, brush);
                Ellipse(dc, center_x - inner_radius, center_y - inner_radius,
                        center_x + inner_radius, center_y + inner_radius);
                SelectObject(dc, previous);
                DeleteObject(brush);
            } else {
                HBRUSH brush = CreateSolidBrush(color);
                HPEN pen = CreatePen(PS_SOLID, 1, color);
                const auto old_brush = SelectObject(dc, brush);
                const auto old_pen = SelectObject(dc, pen);
                Ellipse(dc, center_x - radius, center_y - radius,
                        center_x + radius, center_y + radius);
                SelectObject(dc, old_pen);
                SelectObject(dc, old_brush);
                DeleteObject(pen);
                DeleteObject(brush);
            }
        }
        return TRUE;
    }

    if (is_checkbox(item.hwndItem)) {
        FillRect(dc, &rect, card_background_);
        const int size = px(16, dpi_);
        const int top = rect.top + (rect.bottom - rect.top - size) / 2;
        RECT box{rect.left, top, rect.left + size, top + size};
        const bool checked = checkbox_checked(item.hwndItem);
        fill_round_rect(dc, box, px(4, dpi_), checked ? accent : color_input,
                        checked ? accent : color_line);
        if (checked) {
            HPEN pen = CreatePen(PS_SOLID, px(2, dpi_), RGB(255, 255, 255));
            const auto previous = SelectObject(dc, pen);
            MoveToEx(dc, box.left + px(4, dpi_), box.top + px(8, dpi_), nullptr);
            LineTo(dc, box.left + px(7, dpi_), box.top + px(11, dpi_));
            LineTo(dc, box.left + px(13, dpi_), box.top + px(5, dpi_));
            SelectObject(dc, previous);
            DeleteObject(pen);
        }
        RECT text_rect{box.right + px(9, dpi_), rect.top, rect.right, rect.bottom};
        draw_text(dc, text_of(item.hwndItem), text_rect, font_,
                  disabled ? color_text_faint : color_text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return TRUE;
    }

    const COLORREF fill = disabled ? RGB(30, 32, 38)
                          : pressed ? RGB(35, 38, 45)
                          : hot ? RGB(45, 49, 58) : RGB(36, 39, 46);
    FillRect(dc, &rect, card_background_);
    fill_round_rect(dc, rect, px(6, dpi_), fill, focused ? accent : color_line_high);
    draw_text(dc, text_of(item.hwndItem), rect, font_,
              disabled ? color_text_faint : color_text,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    return TRUE;
}

void SettingsWindow::close() {
    if (!save_values()) return;
    if (text_callbacks_.composition_changed) {
        text_callbacks_.composition_changed(false);
    }
    cancel_model_refresh();
    if (test_thread_.joinable()) test_thread_.request_stop();
    finished_ = true;
    const HWND closing = window_;
    if (closing && IsWindow(closing)) DestroyWindow(closing);
}

LRESULT CALLBACK SettingsWindow::window_proc(HWND window, UINT message,
                                              WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<SettingsWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    try {
        return self ? self->handle_message(message, wparam, lparam)
                    : DefWindowProcW(window, message, wparam, lparam);
    } catch (const std::exception& error) {
        if (self) {
            try { self->window_error_ = error.what(); } catch (...) {}
        }
    } catch (...) {
        if (self) {
            try { self->window_error_ = "unknown settings window error"; } catch (...) {}
        }
    }
    return message == WM_NCCREATE ? FALSE : message == WM_CREATE ? -1 : 0;
}

LRESULT SettingsWindow::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: create_controls(); return 0;
    case WM_SIZE:
        layout_controls();
        if (wparam != SIZE_MINIMIZED) {
            RedrawWindow(window_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        RECT minimum{0, 0, px(760, dpi_), px(590, dpi_)};
        AdjustWindowRectExForDpi(
            &minimum,
            static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE)), FALSE,
            static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_EXSTYLE)),
            static_cast<UINT>(dpi_));
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize.x = minimum.right - minimum.left;
        limits->ptMinTrackSize.y = minimum.bottom - minimum.top;
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wparam);
        const auto* suggested = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        create_theme_resources();
        for (const auto button : navigation_) set_font(button, font_);
        set_font(status_, small_font_);
        for (auto& controls : page_controls_) {
            for (const auto control : controls) {
                set_font(control, font_);
                apply_control_theme(control);
            }
        }
        // The hint labels use a smaller font than the rest of the page, so the
        // rebuild above has to put that font back instead of leaving them
        // stretched to the page font.
        set_font(ocr_engine_note_, small_font_);
        set_font(test_status_, small_font_);
        set_font(provider_hint_, small_font_);
        layout_controls();
        RedrawWindow(window_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notification = HIWORD(wparam);
        const HWND source = reinterpret_cast<HWND>(lparam);
        if (notification == EN_SETFOCUS || notification == EN_KILLFOCUS) {
            InvalidateRect(window_, nullptr, FALSE);
        }
        if (notification == CBN_DROPDOWN && source) {
            set_combo_edit_redraw(source, false);
            style_combo_popup(source, dpi_, font_);
            clear_combo_edit_selection(source);
            InvalidateRect(source, nullptr, FALSE);
        }
        if (notification == CBN_CLOSEUP && source) {
            clear_combo_edit_selection(source);
            align_combo_edit(source, dpi_);
            set_combo_edit_redraw(source, true);
            InvalidateRect(source, nullptr, FALSE);
        }
        if (id == IDCANCEL) { close(); return 0; }
        if (id >= id_nav_first && id <= id_nav_last && notification == BN_CLICKED) {
            save_values(false);
            show_page(navigation_page_order[static_cast<std::size_t>(id - id_nav_first)]);
            return 0;
        }
        if (id == id_text_input && notification == EN_CHANGE) {
            if (!syncing_text_ && text_callbacks_.input_changed) {
                text_callbacks_.input_changed(text_of(text_input_));
            }
            return 0;
        }
        if (id == id_text_target && notification == CBN_SELCHANGE) {
            if (!syncing_text_ && text_callbacks_.target_changed) {
                text_callbacks_.target_changed(text_target_at(text_target_));
            }
            InvalidateRect(text_target_, nullptr, FALSE);
            return 0;
        }
        if (id == id_text_translate && notification == BN_CLICKED) {
            if (text_callbacks_.translate_now) text_callbacks_.translate_now();
            return 0;
        }
        if (id == id_text_clear && notification == BN_CLICKED) {
            if (text_callbacks_.clear) text_callbacks_.clear();
            if (text_input_) SetFocus(text_input_);
            return 0;
        }
        if (id == id_text_copy && notification == BN_CLICKED) {
            if (text_callbacks_.copy) text_callbacks_.copy();
            return 0;
        }
        if (id == id_accent && notification == BN_CLICKED) {
            POINT cursor{};
            GetCursorPos(&cursor);
            ScreenToClient(accent_, &cursor);
            int picked = -1;
            for (std::size_t index = 0; index < accent_swatches.size(); ++index) {
                const int center = px(12 + static_cast<int>(index) * 30, dpi_);
                if (std::abs(cursor.x - center) <= px(12, dpi_)) {
                    picked = static_cast<int>(index);
                    break;
                }
            }
            if (picked < 0) {
                const auto current = text_of(accent_);
                picked = 0;
                for (std::size_t index = 0; index < accent_swatches.size(); ++index) {
                    if (_wcsicmp(current.c_str(),
                                 std::wstring(accent_swatches[index].first).c_str()) == 0) {
                        picked = (static_cast<int>(index) + 1) %
                                 static_cast<int>(accent_swatches.size());
                        break;
                    }
                }
            }
            SetWindowTextW(accent_, accent_swatches[static_cast<std::size_t>(picked)].first.data());
            save_values(false);
            RedrawWindow(window_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            return 0;
        }
        if (!loading_ && notification == BN_CLICKED && is_checkbox(source)) {
            set_checkbox(source, !checkbox_checked(source));
            save_values(false);
            layout_controls();
            RedrawWindow(window_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            return 0;
        }
        if (id == id_translator_class && notification == CBN_SELCHANGE && !loading_) {
            ScopedRedrawPause redraw_pause(window_);
            save_provider_fields();
            cancel_model_refresh(true);
            refresh_provider_fields(true);
            save_values(false);
            layout_controls();
            RedrawWindow(window_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            return 0;
        }
        if (id == id_provider && notification == CBN_SELCHANGE && !loading_) {
            ScopedRedrawPause redraw_pause(window_);
            save_provider_fields();
            cancel_model_refresh(true);
            // The provider combo lists only the traditional entries, so walking
            // the catalog with the same skip rule keeps the mapping correct
            // even if the AI row ever moves away from the end of the table.
            const int chosen = selected_index(provider_);
            int seen = 0;
            loaded_provider_ = providers.front().id;
            for (const auto& provider : providers) {
                if (provider.kind == ProviderInfo::Kind::ai) continue;
                if (seen == chosen) {
                    loaded_provider_ = provider.id;
                    break;
                }
                ++seen;
            }
            config_.set_string(L"translator.provider", loaded_provider_);
            refresh_provider_fields(true);
            save_values(false);
            layout_controls();
            RedrawWindow(window_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            return 0;
        }
        if (source == profile_select_ &&
            (notification == CBN_SELCHANGE || notification == CBN_KILLFOCUS) &&
            !loading_) {
            ScopedRedrawPause redraw_pause(window_);
            save_profile_fields();
            const auto selected = trim(text_of(profile_select_));
            if (!selected.empty() && selected != loaded_profile_) {
                loaded_profile_ = selected;
                config_.set_string(L"translator.ai_selected", loaded_profile_);
            }
            cancel_model_refresh(true);
            refresh_provider_fields(true);
            save_values(false);
            layout_controls();
            RedrawWindow(window_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            return 0;
        }
        if (id == id_ai_protocol && notification == CBN_SELCHANGE && !loading_) {
            save_values(false);
            return 0;
        }
        if (id == id_profile_add && notification == BN_CLICKED) {
            add_profile();
            return 0;
        }
        if (id == id_profile_remove && notification == BN_CLICKED) {
            remove_profile();
            return 0;
        }
        if (id == id_test && notification == BN_CLICKED) {
            test_success_ = false;
            start_connection_test();
            InvalidateRect(test_status_, nullptr, TRUE);
            return 0;
        }
        if (id == id_refresh_models && notification == BN_CLICKED) {
            start_model_refresh();
            return 0;
        }
        if (id == id_open_config && notification == BN_CLICKED) {
            save_values(false);
            const auto arguments = L"/select,\"" + config_.path().wstring() + L"\"";
            ShellExecuteW(window_, L"open", L"explorer.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL);
            return 0;
        }
        if (id == id_check_updates && notification == BN_CLICKED) {
            if (!save_values()) return 0;
            cancel_model_refresh();
            if (updates_callback_) updates_callback_();
            return 0;
        }
        if (id == id_restart && notification == BN_CLICKED) {
            if (save_values() && restart_callback_) {
                cancel_model_refresh();
                finished_ = true;
                ShowWindow(window_, SW_HIDE);
                restart_callback_();
            }
            return 0;
        }
        if (!loading_ && (notification == CBN_SELCHANGE || notification == BN_CLICKED ||
                          notification == EN_KILLFOCUS ||
                          (source == provider_model_ && notification == CBN_KILLFOCUS))) {
            save_values(false);
            if (source == accent_) {
                RedrawWindow(window_, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            }
        }
        break;
    }
    case test_completed_message: {
        std::unique_ptr<std::pair<bool, std::wstring>> result(
            reinterpret_cast<std::pair<bool, std::wstring>*>(lparam));
        if (result) finish_connection_test(result->first, std::move(result->second));
        return 0;
    }
    case models_completed_message: {
        std::unique_ptr<ModelListResult> result(
            reinterpret_cast<ModelListResult*>(lparam));
        if (result) finish_model_refresh(std::move(*result));
        return 0;
    }
    case clear_combo_selection_message: {
        const HWND combo = reinterpret_cast<HWND>(wparam);
        if (combo && IsWindow(combo)) {
            clear_combo_edit_selection(combo);
            align_combo_edit(combo, dpi_);
            RedrawWindow(window_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        return 0;
    }
    case WM_DRAWITEM:
        if (const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam)) {
            return draw_item(*item);
        }
        return FALSE;
    case WM_CTLCOLOREDIT: {
        const auto dc = reinterpret_cast<HDC>(wparam);
        const auto control = reinterpret_cast<HWND>(lparam);
        SetTextColor(dc, control == diagnostic_command_
                             ? RGB(159, 216, 180)
                             : color_text);
        SetBkColor(dc, color_input);
        SetBkMode(dc, OPAQUE);
        return reinterpret_cast<LRESULT>(input_background_);
    }
    case WM_CTLCOLORLISTBOX: {
        const auto dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, color_text);
        SetBkColor(dc, color_input);
        SetBkMode(dc, OPAQUE);
        return reinterpret_cast<LRESULT>(input_background_);
    }
    case WM_CTLCOLORSTATIC: {
        const auto dc = reinterpret_cast<HDC>(wparam);
        const auto control = reinterpret_cast<HWND>(lparam);
        if (control == text_output_) {
            SetTextColor(dc, color_text);
            SetBkColor(dc, color_input);
            SetBkMode(dc, OPAQUE);
            return reinterpret_cast<LRESULT>(input_background_);
        }
        if (control == config_path_ || control == ocr_languages_ ||
            control == capture_hotkey_ || control == toggle_hotkey_ ||
            control == text_hotkey_) {
            SetTextColor(dc, IsWindowEnabled(control) ? color_text_dim : color_text_faint);
            SetBkColor(dc, color_input);
            SetBkMode(dc, OPAQUE);
            return reinterpret_cast<LRESULT>(input_background_);
        }
        SetBkMode(dc, TRANSPARENT);
        if (control == status_) {
            SetTextColor(dc, status_error_ ? color_bad : color_ok);
            return reinterpret_cast<LRESULT>(background_);
        }
        if (control == test_status_) {
            SetTextColor(dc, test_pending_ ? color_text_dim
                              : test_success_ ? color_ok : color_bad);
            return reinterpret_cast<LRESULT>(card_background_);
        }
        const auto control_font = reinterpret_cast<HFONT>(
            SendMessageW(control, WM_GETFONT, 0, 0));
        SetTextColor(dc, control_font == small_font_ ? color_text_dim : color_text);
        return reinterpret_cast<LRESULT>(card_background_);
    }
    case WM_PAINT: paint(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) { close(); return 0; }
        break;
    case WM_CLOSE: close(); return 0;
    case WM_NCDESTROY: {
        const HWND destroyed = window_;
        finished_ = true;
        cancel_model_refresh();
        if (test_thread_.joinable()) {
            test_thread_.request_stop();
            test_thread_.join();
        }
        discard_connection_test_results(destroyed);
        SetWindowLongPtrW(destroyed, GWLP_USERDATA, 0);
        window_ = nullptr;
        return DefWindowProcW(destroyed, message, wparam, lparam);
    }
    default: break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace screentrans
