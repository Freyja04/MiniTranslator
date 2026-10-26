#pragma once

// Shared internals for the text translation window: the window class ids,
// small helpers and the output-editor subclass live here so the message
// handler split can stay two translation units without exposing them.

#include "text_translation_window.hpp"

#include "self_test_support.hpp"

#include "language.hpp"
#include "resource.h"
#include "theme.hpp"
#include "ui_controls.hpp"
#include "ui_icons.hpp"
#include "util.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <memory>

namespace minitrans {

namespace {

constexpr wchar_t quick_window_class[] = L"MiniTranslator.Native.QuickTranslateWindow.v1";
constexpr UINT_PTR input_subclass_id = 11;
constexpr UINT_PTR target_combo_subclass_id = 12;
constexpr UINT_PTR source_combo_subclass_id = 13;

using theme::color_background;
using theme::color_border;
using theme::color_dim;
using theme::color_error;
using theme::color_faint;
using theme::color_hover;
using theme::color_input;
using theme::color_line_high;
using theme::color_surface;
using theme::color_text;
using theme::font_size_small;
using theme::font_size_translation;
constexpr int quick_window_logical_width = 480;
constexpr int quick_window_logical_height = 320;

constexpr int id_target = 4101;
constexpr int id_input = 4102;
constexpr int id_output = 4103;
constexpr int id_clear = 4104;
constexpr int id_copy = 4105;
constexpr int id_quick_capture = 4109;
constexpr int id_source = 4110;
constexpr int id_swap = 4111;
constexpr int id_quick_close = 4112;
constexpr int id_quick_home = 4113;

using self_test_support::ProcessResourceSnapshot;
using self_test_support::process_resources;
using self_test_support::resource_change;
using self_test_support::resource_series_stable;
using self_test_support::settle_self_test_ui;


HMENU control_id(int id) noexcept {
    return reinterpret_cast<HMENU>(static_cast<INT_PTR>(id));
}

int scaled(int value, int dpi) noexcept {
    return MulDiv(value, dpi, 96);
}

std::wstring window_text(HWND control) {
    if (!control) return {};
    const int length = GetWindowTextLengthW(control);
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

void register_window_class(HINSTANCE instance, const wchar_t* name, WNDPROC procedure) {
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.style = CS_HREDRAW | CS_VREDRAW;
    description.lpfnWndProc = procedure;
    description.hInstance = instance;
    description.hIcon = LoadIconW(instance, MAKEINTRESOURCEW(IDI_APP_ICON));
    description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    description.lpszClassName = name;
    if (!RegisterClassExW(&description) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw_last_error("register text translation window");
    }
}

void apply_dark_frame(HWND window, bool quick) {
    theme::apply_window_theme(window, true, !quick);
}

HFONT create_font(int dpi, int logical_pixels, int weight,
                  const std::wstring& family) {
    return CreateFontW(-scaled(logical_pixels, dpi), 0, 0, 0, weight, FALSE, FALSE,
                       FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                       CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, family.c_str());
}

int CALLBACK find_icon_font_family(const LOGFONTW*, const TEXTMETRICW*, DWORD,
                                   LPARAM data) {
    *reinterpret_cast<bool*>(data) = true;
    return 0;
}

bool icon_font_family_available(HDC dc, const wchar_t* face) {
    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    if (wcsncpy_s(query.lfFaceName, LF_FACESIZE, face, _TRUNCATE) != 0) {
        return false;
    }
    bool found = false;
    EnumFontFamiliesExW(dc, &query, find_icon_font_family,
                        reinterpret_cast<LPARAM>(&found), 0);
    return found;
}

HFONT create_icon_font(int dpi, int logical_pixels) {
    // Segoe Fluent Icons only ships with newer Windows builds. Fall back to
    // Segoe MDL2 Assets so the icon buttons also render on Windows 10.
    constexpr std::array<const wchar_t*, 3> faces{
        L"Segoe Fluent Icons", L"Segoe MDL2 Assets", L"Segoe UI Symbol",
    };
    HDC dc = GetDC(nullptr);
    for (const auto* face : faces) {
        if (!dc || icon_font_family_available(dc, face)) {
            if (dc) ReleaseDC(nullptr, dc);
            return create_font(dpi, logical_pixels, FW_NORMAL, face);
        }
    }
    if (dc) ReleaseDC(nullptr, dc);
    return nullptr;
}

void set_control_font(HWND control, HFONT font) {
    if (control && font) SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
}

void add_tooltip(HWND owner, HWND control, const wchar_t* text) {
    HWND tooltip = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        owner, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!tooltip) return;
    theme::apply_control_theme(tooltip);
    SendMessageW(tooltip, TTM_SETTIPBKCOLOR, static_cast<WPARAM>(color_surface), 0);
    SendMessageW(tooltip, TTM_SETTIPTEXTCOLOR, static_cast<WPARAM>(color_text), 0);
    TOOLINFOW tool{};
    tool.cbSize = TTTOOLINFOW_V2_SIZE;
    tool.uFlags = TTF_IDISHWND | TTF_SUBCLASS | TTF_TRANSPARENT;
    tool.hwnd = owner;
    tool.uId = reinterpret_cast<UINT_PTR>(control);
    tool.lpszText = const_cast<LPWSTR>(text);
    if (!SendMessageW(tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&tool))) {
        DestroyWindow(tooltip);
    }
}

void populate_targets(HWND combo, bool include_auto) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    if (include_auto) {
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"自动选择"));
    }
    for (const auto target : text_translation_targets()) {
        const auto label = target_display_name(target);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
    }
    SendMessageW(combo, CB_SETCURSEL, 0, 0);
}

int target_index(std::wstring_view target, bool include_auto) {
    const int offset = include_auto ? 1 : 0;
    if (target.empty()) return include_auto ? 0 : -1;
    const auto& targets = text_translation_targets();
    const auto found = std::find(targets.begin(), targets.end(), target);
    return found == targets.end()
               ? (include_auto ? 0 : -1)
               : static_cast<int>(std::distance(targets.begin(), found)) + offset;
}

std::wstring target_at(HWND combo, bool include_auto) {
    const LRESULT selected = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    const int offset = include_auto ? 1 : 0;
    if (selected < offset) return {};
    const auto& targets = text_translation_targets();
    const auto index = static_cast<std::size_t>(selected - offset);
    return index < targets.size() ? std::wstring(targets[index]) : std::wstring{};
}

std::wstring session_status(const TextTranslationSession& session) {
    switch (session.state()) {
    case TextTranslationState::idle:
        return L"等待输入中...";
    case TextTranslationState::waiting:
        return L"等待输入完成…";
    case TextTranslationState::translating:
        return L"正在翻译为" + target_display_name(session.effective_target()) + L"…";
    case TextTranslationState::ready:
        return L"已翻译为" + target_display_name(session.effective_target());
    case TextTranslationState::error:
        return session.error().empty() ? L"文字翻译失败" : session.error();
    case TextTranslationState::too_long:
        return L"最多支持 500 个字符，请删减后重试";
    }
    return {};
}

bool message_for(HWND window, const MSG& message) noexcept {
    return window && (message.hwnd == window || IsChild(window, message.hwnd));
}

void fill_rect(HDC dc, const RECT& rect, COLORREF color) {
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &rect, brush);
    DeleteObject(brush);
}

void draw_rounded_control(HDC dc, RECT bounds, COLORREF fill, COLORREF border,
                          int radius) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    const HGDIOBJ old_brush = SelectObject(dc, brush);
    const HGDIOBJ old_pen = SelectObject(dc, pen);
    RoundRect(dc, bounds.left, bounds.top, bounds.right, bounds.bottom, radius, radius);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

void draw_label(HDC dc, std::wstring_view text, RECT bounds, HFONT font,
                COLORREF color, UINT format) {
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    const HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &bounds,
              format | DT_NOPREFIX);
    if (previous) SelectObject(dc, previous);
}

int measure_label_width(HWND window, HFONT font, std::wstring_view text) {
    HDC dc = GetDC(window);
    if (!dc) return 0;
    const HGDIOBJ previous = font ? SelectObject(dc, font) : nullptr;
    SIZE extent{};
    GetTextExtentPoint32W(dc, text.data(), static_cast<int>(text.size()), &extent);
    if (previous) SelectObject(dc, previous);
    ReleaseDC(window, dc);
    return extent.cx;
}

std::wstring target_selector_text(const TextTranslationSession& session,
                                  bool compact = false) {
    // The target is always a concrete language now; the auto picker is gone.
    auto target = target_display_name(session.effective_target());
    if (compact) {
        if (session.effective_target() == L"zh-Hans") target = L"中文";
        else if (session.effective_target() == L"zh-Hant") target = L"繁中";
    }
    return target;
}

std::wstring source_selector_text(const TextTranslationSession& session) {
    return session.source().empty() ? std::wstring(L"自动识别")
                                    : target_display_name(session.source());
}

void quick_round_rect(HDC dc, const RECT& bounds, int radius, COLORREF fill,
                      COLORREF border) {
    draw_rounded_control(dc, bounds, fill, border, radius);
}

void draw_target_selector(HWND combo, HDC dc, HFONT font, int dpi,
                          COLORREF accent, std::wstring_view value) {
    if (!combo || !dc) return;
    RECT bounds{};
    GetClientRect(combo, &bounds);
    ui_controls::FieldStyle style;
    style.background = color_background;
    style.fill = color_input;
    style.border = color_border;
    style.focus_border = accent;
    style.text = color_text;
    style.disabled_text = color_faint;
    style.chevron = color_dim;
    style.font = font;
    style.dpi = dpi;
    style.focused = GetFocus() == combo;
    ui_controls::draw_field(dc, bounds, value, style, quick_round_rect);
}

void style_target_popup(HWND combo, int dpi, bool update_selection_height = true) {
    if (!combo) return;
    if (update_selection_height) {
        SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), scaled(30, dpi));
    }
    SendMessageW(combo, CB_SETITEMHEIGHT, 0, scaled(30, dpi));
    // Keep short language lists fully visible so the dropdown matches the
    // settings window instead of showing a classic scrollbar.
    SendMessageW(combo, CB_SETMINVISIBLE, 12, 0);

    // The closed selector is narrow, so widen the dropdown to the longest
    // language name instead of truncating every entry.
    RECT combo_rect{};
    GetWindowRect(combo, &combo_rect);
    int dropped_width = std::max(1, static_cast<int>(combo_rect.right - combo_rect.left));
    HDC dc = GetDC(combo);
    const HFONT font = reinterpret_cast<HFONT>(SendMessageW(combo, WM_GETFONT, 0, 0));
    const HGDIOBJ old_font = (dc && font) ? SelectObject(dc, font) : nullptr;
    const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int index = 0; dc && index < count; ++index) {
        const int length = static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, index, 0));
        if (length <= 0) continue;
        std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
        SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(value.data()));
        SIZE measured{};
        if (GetTextExtentPoint32W(dc, value.data(), length, &measured)) {
            dropped_width =
                std::max(dropped_width, static_cast<int>(measured.cx) + scaled(44, dpi));
        }
    }
    if (dc) {
        if (old_font) SelectObject(dc, old_font);
        ReleaseDC(combo, dc);
    }
    SendMessageW(combo, CB_SETDROPPEDWIDTH,
                 std::min(dropped_width, scaled(320, dpi)), 0);

    COMBOBOXINFO info{sizeof(info)};
    if (!GetComboBoxInfo(combo, &info) || !info.hwndList) return;
    theme::apply_window_theme(info.hwndList);
    const LONG_PTR ex_style = GetWindowLongPtrW(info.hwndList, GWL_EXSTYLE);
    SetWindowLongPtrW(info.hwndList, GWL_EXSTYLE,
                      ex_style & ~static_cast<LONG_PTR>(WS_EX_CLIENTEDGE));
    SetWindowPos(info.hwndList, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);

    // The list keeps the height it received on its first show, and that height
    // is roughly eight rows regardless of CB_SETMINVISIBLE. Size it right
    // before the drop-down appears so the language list never needs the
    // classic scrollbar.
    RECT list{};
    if (count <= 0 || !GetWindowRect(info.hwndList, &list)) return;
    const int item_height =
        static_cast<int>(SendMessageW(combo, CB_GETITEMHEIGHT, 0, 0));
    if (item_height <= 0) return;
    const int rows = std::clamp(count, 1, 12);
    const int height = rows * item_height + 2;
    // A drop-down that Windows already flipped above the combo has to grow
    // upwards as well, otherwise the extra rows would cover the combo.
    int top = list.bottom <= combo_rect.top ? static_cast<int>(list.bottom) - height
                                            : static_cast<int>(list.top);
    MONITORINFO monitor{sizeof(monitor)};
    if (GetMonitorInfoW(MonitorFromWindow(info.hwndList, MONITOR_DEFAULTTONEAREST),
                        &monitor)) {
        const int min_top = static_cast<int>(monitor.rcWork.top);
        const int max_top = static_cast<int>(monitor.rcWork.bottom) - height;
        top = std::max(min_top, std::min(top, std::max(min_top, max_top)));
    }
    SetWindowPos(info.hwndList, HWND_TOPMOST, list.left, top,
                 static_cast<int>(list.right - list.left), height,
                 SWP_NOACTIVATE);
}

bool point_in_window(HWND control, POINT screen_point) noexcept {
    RECT bounds{};
    return control && GetWindowRect(control, &bounds) &&
           PtInRect(&bounds, screen_point) != FALSE;
}

LRESULT draw_owner_item(const DRAWITEMSTRUCT& item, HFONT font, COLORREF accent,
                        HFONT icon_font, HFONT action_icon_font,
                        HFONT close_icon_font) {
    if (item.CtlType == ODT_BUTTON) {
        const bool selected = (item.itemState & ODS_SELECTED) != 0;
        const bool disabled = (item.itemState & ODS_DISABLED) != 0;
        const bool focused = (item.itemState & ODS_FOCUS) != 0;
        const bool hot = (item.itemState & ODS_HOTLIGHT) != 0;
        const int identifier = GetDlgCtrlID(item.hwndItem);
        // Clear/copy are borderless icons that sit on the card surface, so they
        // repaint the surface colour; the page colour would leave a visible box
        // behind the glyph.
        const bool on_card = identifier == id_copy || identifier == id_clear;
        SetDCBrushColor(item.hDC, on_card ? color_surface : color_background);
        FillRect(item.hDC, &item.rcItem,
                 reinterpret_cast<HBRUSH>(GetStockObject(DC_BRUSH)));

        wchar_t glyph = 0;
        HFONT glyph_font = icon_font;
        if (identifier == id_quick_capture) {
            glyph = ui_icons::capture_translation;
        } else if (identifier == id_quick_close) {
            glyph = 0xE8BB;  // ChromeClose
            // The close cross reads heavier than the capture glyph at the same
            // font size, so draw it with its own slightly smaller icon font.
            if (close_icon_font) glyph_font = close_icon_font;
        } else if (identifier == id_quick_home) {
            glyph = 0xE80F;  // Home
        } else if (identifier == id_swap) {
            glyph = 0xE8AB;
        } else if (identifier == id_copy) {
            glyph = ui_icons::copy;
            if (action_icon_font) glyph_font = action_icon_font;
        } else if (identifier == id_clear) {
            glyph = ui_icons::delete_glyph;
            if (action_icon_font) glyph_font = action_icon_font;
        } else {
            return TRUE;
        }

        const COLORREF content_color = disabled ? color_faint
                                        : hot || focused ? color_text : color_dim;
        const bool boxed = identifier == id_quick_capture ||
                           identifier == id_quick_home ||
                           identifier == id_quick_close;
        if (boxed) {
            const int dpi = static_cast<int>(GetDpiForWindow(item.hwndItem));
            const COLORREF fill = selected ? color_hover
                                  : hot ? theme::color_line_high : color_surface;
            ui_controls::draw_icon_button(
                item.hDC, item.rcItem, glyph, glyph_font, dpi, fill,
                focused || hot ? accent : color_border, content_color,
                quick_round_rect);
            return TRUE;
        }
        draw_label(item.hDC, std::wstring_view(&glyph, 1), item.rcItem,
                   glyph_font, content_color,
                   DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }
    if (item.CtlType == ODT_COMBOBOX) {
        const bool list_item = item.itemID != static_cast<UINT>(-1);
        const int dpi = static_cast<int>(GetDpiForWindow(item.hwndItem));
        std::wstring text;
        if (list_item) {
            const LRESULT length =
                SendMessageW(item.hwndItem, CB_GETLBTEXTLEN, item.itemID, 0);
            if (length >= 0) {
                text.resize(static_cast<std::size_t>(length) + 1);
                SendMessageW(item.hwndItem, CB_GETLBTEXT, item.itemID,
                             reinterpret_cast<LPARAM>(text.data()));
                text.resize(static_cast<std::size_t>(length));
            }
        }
        ui_controls::FieldItemStyle style;
        style.background = color_input;
        style.text = color_text;
        style.disabled_text = color_faint;
        style.accent = theme::color_line_high;
        style.font = font;
        style.dpi = dpi;
        style.selected = list_item && (item.itemState & ODS_SELECTED) != 0;
        style.disabled = (item.itemState & ODS_DISABLED) != 0;
        ui_controls::draw_field_item(item.hDC, item.rcItem, text, style,
                                     quick_round_rect);
        return TRUE;
    }
    return FALSE;
}

void position_near_anchor(HWND window, POINT anchor) {
    const HMONITOR monitor = MonitorFromPoint(anchor, MONITOR_DEFAULTTONEAREST);
    MONITORINFO info{sizeof(info)};
    GetMonitorInfoW(monitor, &info);
    const int dpi = static_cast<int>(GetDpiForWindow(window));
    const int width = scaled(quick_window_logical_width, dpi);
    const int height = scaled(quick_window_logical_height, dpi);
    const int work_left = info.rcWork.left;
    const int work_top = info.rcWork.top;
    const int work_right = info.rcWork.right;
    const int work_bottom = info.rcWork.bottom;
    const int work_width = work_right - work_left;
    const int work_height = work_bottom - work_top;

    // Grow away from the edge that holds the tray icon so the panel never
    // overlaps the taskbar.
    const int gap = scaled(10, dpi);
    const int panel_width = std::min(
        width, std::max(scaled(320, dpi), work_width - gap * 2));
    const int panel_height = std::min(
        height, std::max(scaled(200, dpi), work_height - gap * 2));
    const bool anchor_bottom = anchor.y > work_top + work_height / 2;
    const bool anchor_top = anchor.y < work_top + work_height / 4;
    int x = anchor.x - panel_width / 2;
    int y = anchor_bottom ? anchor.y - panel_height - gap : anchor.y + gap;
    if (anchor_top && !anchor_bottom) y = std::max(work_top + gap, y);
    x = std::clamp(x, work_left + gap,
                   std::max(work_left + gap, work_right - panel_width - gap));
    y = std::clamp(y, work_top + gap,
                   std::max(work_top + gap, work_bottom - panel_height - gap));

    SetWindowPos(window, HWND_TOPMOST, x, y, panel_width, panel_height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

POINT default_quick_anchor() noexcept {
    const HMONITOR monitor = MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(info)};
    if (monitor && GetMonitorInfoW(monitor, &info)) {
        return POINT{info.rcWork.right, info.rcWork.bottom};
    }
    return POINT{GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
}

}  // namespace

namespace {
constexpr UINT_PTR output_editor_subclass_id = 1;

// The translation output is a display-only editor: focus attempts are sent to
// the source editor so the output never shows a caret or a selection frame.
LRESULT CALLBACK keep_editor_unfocused(HWND control, UINT message, WPARAM wparam,
                                       LPARAM lparam, UINT_PTR, DWORD_PTR reference) {
    if (message == WM_SETFOCUS || message == WM_LBUTTONDOWN ||
        message == WM_LBUTTONDBLCLK) {
        const auto target = reinterpret_cast<HWND>(reference);
        if (target && IsWindow(target)) SetFocus(target);
        return 0;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}
}  // namespace

}  // namespace minitrans
