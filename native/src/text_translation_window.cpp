#include "text_translation_window.hpp"

#include "language.hpp"
#include "resource.h"
#include "self_test_support.hpp"
#include "theme.hpp"
#include "ui_controls.hpp"
#include "ui_icons.hpp"
#include "util.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <array>

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
    if (!session.target().empty()) return target_display_name(session.target());
    if (session.input().find_first_not_of(L" \t\r\n") == std::wstring::npos) {
        return L"自动选择";
    }
    auto target = target_display_name(session.effective_target());
    if (compact) {
        if (session.effective_target() == L"zh-Hans") target = L"中文";
        else if (session.effective_target() == L"zh-Hant") target = L"繁中";
    }
    return L"自动 · " + target;
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
        style.accent = accent;
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

QuickTranslateWindow::QuickTranslateWindow(HINSTANCE instance,
                                           TextTranslationSession& session)
    : instance_(instance), session_(session) {}

QuickTranslateWindow::~QuickTranslateWindow() {
    if (window_) DestroyWindow(window_);
    destroy_theme_resources();
}

bool QuickTranslateWindow::visible() const noexcept {
    return window_ && IsWindowVisible(window_);
}

void QuickTranslateWindow::create_window(HWND owner) {
    if (window_) return;
    register_window_class(instance_, quick_window_class, &QuickTranslateWindow::window_proc);
    window_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, quick_window_class, MINITRANS_DISPLAY_NAME,
        WS_POPUP | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        quick_window_logical_width, quick_window_logical_height,
        owner, nullptr, instance_, this);
    if (!window_) throw_last_error("create quick translation window");
    dpi_ = static_cast<int>(GetDpiForWindow(window_));
    apply_dark_frame(window_, true);
    create_theme_resources();
    create_controls();
    refresh();
}

void QuickTranslateWindow::create_theme_resources() {
    destroy_theme_resources();
    accent_ = theme::accent;
    font_ = create_font(dpi_, font_size_translation, FW_NORMAL,
                        theme::font_family);
    small_font_ = create_font(dpi_, font_size_small, FW_NORMAL,
                              theme::font_family);
    icon_font_ = create_icon_font(dpi_, 15);
    action_icon_font_ = create_icon_font(dpi_, 12);
    // The close cross reads heavier than the capture glyph at the same size.
    close_icon_font_ = create_icon_font(dpi_, 13);
    background_ = CreateSolidBrush(color_background);
    surface_ = CreateSolidBrush(color_surface);
    input_background_ = CreateSolidBrush(color_surface);
}

void QuickTranslateWindow::destroy_theme_resources() noexcept {
    if (font_) DeleteObject(font_);
    if (small_font_) DeleteObject(small_font_);
    if (icon_font_) DeleteObject(icon_font_);
    if (action_icon_font_) DeleteObject(action_icon_font_);
    if (close_icon_font_) DeleteObject(close_icon_font_);
    if (background_) DeleteObject(background_);
    if (surface_) DeleteObject(surface_);
    if (input_background_) DeleteObject(input_background_);
    font_ = small_font_ = icon_font_ = action_icon_font_ = nullptr;
    close_icon_font_ = nullptr;
    background_ = surface_ = input_background_ = nullptr;
}

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

void QuickTranslateWindow::create_controls() {
    source_combo_ = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
        0, 0, 0, 0, window_, control_id(id_source), instance_, nullptr);
    swap_button_ = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, window_, control_id(id_swap), instance_, nullptr);
    target_combo_ = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
        0, 0, 0, 0, window_, control_id(id_target), instance_, nullptr);
    input_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        0, 0, 0, 0, window_, control_id(id_input), instance_, nullptr);
    output_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        0, 0, 0, 0, window_, control_id(id_output), instance_, nullptr);
    capture_button_ = CreateWindowExW(0, L"BUTTON", L"截图翻译",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, window_, control_id(id_quick_capture), instance_, nullptr);
    home_button_ = CreateWindowExW(0, L"BUTTON", L"首页",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, window_, control_id(id_quick_home), instance_, nullptr);
    close_button_ = CreateWindowExW(0, L"BUTTON", L"关闭",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, window_, control_id(id_quick_close), instance_, nullptr);
    copy_button_ = CreateWindowExW(0, L"BUTTON", L"复制",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, window_, control_id(id_copy), instance_, nullptr);
    clear_button_ = CreateWindowExW(0, L"BUTTON", L"清空",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, window_, control_id(id_clear), instance_, nullptr);
    for (HWND control : {source_combo_, swap_button_, target_combo_, input_, output_,
                         capture_button_, home_button_, close_button_, copy_button_,
                         clear_button_}) {
        theme::apply_control_theme(control);
        set_control_font(control, font_);
    }
    // No left margin: the text starts at the same x as the 原文/译文 captions.
    SendMessageW(input_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(0, scaled(12, dpi_)));
    SendMessageW(output_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(0, scaled(12, dpi_)));
    SendMessageW(input_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"输入或粘贴要翻译的文字"));
    SendMessageW(output_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"译文"));
    SetWindowSubclass(output_, &keep_editor_unfocused, output_editor_subclass_id,
                      reinterpret_cast<DWORD_PTR>(input_));
    populate_targets(source_combo_, false);
    populate_targets(target_combo_, true);
    SetWindowSubclass(source_combo_, &QuickTranslateWindow::target_combo_proc,
                      source_combo_subclass_id,
                      reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(target_combo_, &QuickTranslateWindow::target_combo_proc,
                      target_combo_subclass_id,
                      reinterpret_cast<DWORD_PTR>(this));
    style_target_popup(source_combo_, dpi_);
    style_target_popup(target_combo_, dpi_);
    SetWindowSubclass(input_, &QuickTranslateWindow::input_proc, input_subclass_id,
                      reinterpret_cast<DWORD_PTR>(this));
    add_tooltip(window_, copy_button_, L"复制译文");
    add_tooltip(window_, clear_button_, L"清空原文");
    add_tooltip(window_, swap_button_, L"对调原文与译文语种");
    add_tooltip(window_, capture_button_, L"截图翻译");
    add_tooltip(window_, home_button_, L"首页");
    add_tooltip(window_, close_button_, L"关闭");
    layout_controls();
}
void QuickTranslateWindow::layout_controls() {
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right;
    const int height = client.bottom;
    const int margin = scaled(16, dpi_);
    const int gap = scaled(8, dpi_);
    const int header_top = scaled(10, dpi_);
    const int control_height = scaled(30, dpi_);
    const int icon_width = scaled(30, dpi_);
    const int combo_width = scaled(96, dpi_);
    // A drop-down combo uses its window height for the open list as well, so
    // size it from the item count: enough rows to avoid a scrollbar, but not
    // taller than the twelve-row cap shared with the settings window.
    const auto combo_height_for = [&](HWND combo) {
        const int count = std::clamp(
            static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0)), 1, 12);
        return control_height + count * scaled(30, dpi_);
    };

    // The language selectors sit on the left edge; capture, home and close
    // share the right edge in that order.
    MoveWindow(source_combo_, margin, header_top, combo_width,
               combo_height_for(source_combo_), TRUE);
    MoveWindow(swap_button_, margin + combo_width + gap, header_top, icon_width,
               control_height, TRUE);
    MoveWindow(target_combo_, margin + combo_width + gap * 2 + icon_width,
               header_top, combo_width, combo_height_for(target_combo_), TRUE);
    MoveWindow(close_button_, width - margin - icon_width, header_top,
               icon_width, control_height, TRUE);
    MoveWindow(home_button_, width - margin - icon_width * 2 - gap, header_top,
               icon_width, control_height, TRUE);
    MoveWindow(capture_button_, width - margin - icon_width * 3 - gap * 2,
               header_top, icon_width, control_height, TRUE);

    const int body_top = header_top + control_height + scaled(14, dpi_);
    const int body_bottom = height - scaled(34, dpi_);
    const int inset = scaled(13, dpi_);
    const int label_height = scaled(29, dpi_);
    const int action_width = scaled(26, dpi_);
    const int action_height = scaled(22, dpi_);
    const int divider_y = body_top + (body_bottom - body_top) / 2;
    const int content_right = width - margin - inset;
    const int edit_left = margin + inset;
    const int edit_width = std::max(1, content_right - edit_left);

    MoveWindow(input_, edit_left, body_top + label_height, edit_width,
               std::max(1, divider_y - body_top - label_height), TRUE);
    MoveWindow(output_, edit_left, divider_y + label_height, edit_width,
               std::max(1, body_bottom - divider_y - label_height - 1), TRUE);

    // The clear and copy buttons follow their 原文/译文 captions instead of the
    // card's right edge, so the left offset tracks the measured caption advance.
    const int caption_advance =
        std::max(measure_label_width(window_, small_font_, L"原文"),
                 measure_label_width(window_, small_font_, L"译文"));
    const int action_left = edit_left + caption_advance + scaled(6, dpi_);
    const int action_offset = (label_height - action_height) / 2;
    MoveWindow(clear_button_, action_left, body_top + action_offset,
               action_width, action_height, TRUE);
    MoveWindow(copy_button_, action_left, divider_y + action_offset,
               action_width, action_height, TRUE);
}

void QuickTranslateWindow::paint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window_, &paint);
    RECT client{};
    GetClientRect(window_, &client);
    FillRect(dc, &client, background_);

    const int margin = scaled(16, dpi_);
    const int inset = scaled(13, dpi_);
    const int label_height = scaled(29, dpi_);
    const int header_top = scaled(10, dpi_);
    const int control_height = scaled(30, dpi_);

    const int body_top = header_top + control_height + scaled(14, dpi_);
    const int body_bottom = client.bottom - scaled(34, dpi_);
    RECT body{margin, body_top, client.right - margin, body_bottom};
    draw_rounded_control(dc, body, color_surface, color_border, scaled(10, dpi_));

    const int divider_y = body_top + (body_bottom - body_top) / 2;
    HPEN divider = CreatePen(PS_SOLID, 1, color_border);
    const HGDIOBJ previous_pen = SelectObject(dc, divider);
    MoveToEx(dc, body.left + inset, divider_y, nullptr);
    LineTo(dc, body.right - inset, divider_y);
    SelectObject(dc, previous_pen);
    DeleteObject(divider);

    const int content_left = body.left + inset;
    draw_label(dc, L"原文",
               RECT{content_left, body.top, content_left + scaled(60, dpi_),
                    body.top + label_height},
               small_font_, color_dim,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_label(dc, L"译文",
               RECT{content_left, divider_y, content_left + scaled(60, dpi_),
                    divider_y + label_height},
               small_font_, color_dim,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const bool copied = session_.copy_confirmation_visible();
    const std::wstring footer = copied
                                    ? std::wstring(L"已复制到剪贴板")
                                    : session_.state() == TextTranslationState::too_long
                                          ? std::wstring(L"已超出 500 字上限")
                                          : session_status(session_);
    draw_label(dc, footer,
               RECT{margin + scaled(2, dpi_), body.bottom + scaled(6, dpi_),
                    client.right - margin - scaled(200, dpi_),
                    client.bottom - scaled(8, dpi_)},
               small_font_,
               status_error_ ? color_error : color_dim,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    draw_label(dc, L"Ctrl+Enter 翻译 · Esc 关闭",
               RECT{margin, body.bottom + scaled(6, dpi_), client.right - margin,
                    client.bottom - scaled(8, dpi_)},
               small_font_, color_faint,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    EndPaint(window_, &paint);
}

void QuickTranslateWindow::sync_input() {
    if (window_text(input_) == session_.input()) return;
    syncing_ = true;
    SetWindowTextW(input_, session_.input().c_str());
    syncing_ = false;
}

void QuickTranslateWindow::sync_source() {
    const int expected = target_index(session_.source(), false);
    if (SendMessageW(source_combo_, CB_GETCURSEL, 0, 0) == expected) return;
    syncing_ = true;
    SendMessageW(source_combo_, CB_SETCURSEL, std::max(0, expected), 0);
    syncing_ = false;
}

void QuickTranslateWindow::sync_target() {
    const int expected = target_index(session_.target(), true);
    if (SendMessageW(target_combo_, CB_GETCURSEL, 0, 0) == expected) return;
    syncing_ = true;
    SendMessageW(target_combo_, CB_SETCURSEL, expected, 0);
    syncing_ = false;
}

void QuickTranslateWindow::refresh() {
    if (!window_) return;
    sync_input();
    sync_source();
    sync_target();
    if (window_text(output_) != session_.output()) {
        SetWindowTextW(output_, session_.output().c_str());
    }
    status_error_ = session_.state() == TextTranslationState::error ||
                    session_.state() == TextTranslationState::too_long;
    EnableWindow(copy_button_, !session_.output().empty());
    EnableWindow(clear_button_, !session_.input().empty());
    InvalidateRect(input_, nullptr, FALSE);
    InvalidateRect(source_combo_, nullptr, FALSE);
    InvalidateRect(target_combo_, nullptr, FALSE);
    InvalidateRect(window_, nullptr, FALSE);
}

void QuickTranslateWindow::refresh_appearance() {
    if (!window_) return;
    create_theme_resources();
    // The action buttons follow the width of the "原文"/"译文" captions, so a
    // new UI font can move them; re-run the layout after rebuilding the fonts.
    layout_controls();
    for (HWND control : {source_combo_, swap_button_, target_combo_, input_, output_,
                         capture_button_, copy_button_, clear_button_}) {
        set_control_font(control, font_);
    }
    apply_dark_frame(window_, true);
    style_target_popup(source_combo_, dpi_);
    style_target_popup(target_combo_, dpi_);
    InvalidateRect(window_, nullptr, TRUE);
    InvalidateRect(source_combo_, nullptr, TRUE);
    InvalidateRect(target_combo_, nullptr, TRUE);
}

void QuickTranslateWindow::show(HWND owner) {
    show_at(owner, default_quick_anchor(), true);
}

void QuickTranslateWindow::show_at(HWND owner, POINT anchor, bool select_all) {
    create_window(owner);
    refresh();
    position_near_anchor(window_, anchor);
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
    SetFocus(input_);
    if (select_all) SendMessageW(input_, EM_SETSEL, 0, -1);
}

void QuickTranslateWindow::toggle_near(HWND owner, POINT anchor, bool select_all) {
    if (visible()) hide();
    else show_at(owner, anchor, select_all);
}

void QuickTranslateWindow::hide() {
    const bool was_visible = window_ && IsWindowVisible(window_);
    if (window_) ShowWindow(window_, SW_HIDE);
    if (callbacks_.composition_changed) callbacks_.composition_changed(false);
    // A hidden window cannot show a translation that is still on the wire, so
    // let the host cancel it instead of letting it finish in the background.
    if (was_visible && callbacks_.hidden) callbacks_.hidden();
}

void QuickTranslateWindow::on_input_changed() {
    if (!syncing_ && callbacks_.input_changed) {
        callbacks_.input_changed(window_text(input_));
    }
}

void QuickTranslateWindow::on_source_changed() {
    if (syncing_) return;
    const std::wstring source = target_at(source_combo_, false);
    if (callbacks_.source_changed) callbacks_.source_changed(source);
}

void QuickTranslateWindow::on_target_changed() {
    if (syncing_) return;
    const std::wstring target = target_at(target_combo_, true);
    if (callbacks_.target_changed) callbacks_.target_changed(target);
}

void QuickTranslateWindow::swap_languages() {
    const std::wstring source = target_at(source_combo_, false);
    const std::wstring target = target_at(target_combo_, true);
    if (source.empty() || target.empty() || source == target) return;
    session_.set_source(target);
    session_.set_target(source);
    syncing_ = true;
    SendMessageW(source_combo_, CB_SETCURSEL, target_index(target, false), 0);
    SendMessageW(target_combo_, CB_SETCURSEL, target_index(source, true), 0);
    syncing_ = false;
    if (callbacks_.source_changed) callbacks_.source_changed(session_.source());
    if (callbacks_.target_changed) callbacks_.target_changed(session_.target());
    // Swapping the pair invalidates the text that was typed for the old
    // direction, so clear the editor instead of translating stale input.
    if (callbacks_.clear) callbacks_.clear();
    InvalidateRect(source_combo_, nullptr, FALSE);
    InvalidateRect(target_combo_, nullptr, FALSE);
    SetFocus(input_);
}

LRESULT QuickTranslateWindow::draw_item(const DRAWITEMSTRUCT& item) {
    if (item.CtlType == ODT_COMBOBOX && (item.itemState & ODS_COMBOBOXEDIT) != 0) {
        const bool source = GetDlgCtrlID(item.hwndItem) == id_source;
        fill_rect(item.hDC, item.rcItem, color_input);
        RECT text_bounds = item.rcItem;
        text_bounds.left += scaled(8, dpi_);
        text_bounds.right -= scaled(4, dpi_);
        draw_label(item.hDC,
                   source ? source_selector_text(session_)
                          : target_selector_text(session_, true),
                   text_bounds, small_font_, color_text,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return TRUE;
    }
    return draw_owner_item(item, font_, accent_, icon_font_, action_icon_font_,
                           close_icon_font_);
}

void QuickTranslateWindow::draw_target_combo(HDC dc) {
    draw_target_selector(target_combo_, dc, small_font_, dpi_, accent_,
                         target_selector_text(session_, true));
}

void QuickTranslateWindow::draw_source_combo(HDC dc) {
    draw_target_selector(source_combo_, dc, small_font_, dpi_, accent_,
                         source_selector_text(session_));
}

bool QuickTranslateWindow::preprocess_message(MSG& message) {
    if (!visible() || !message_for(window_, message)) return false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
        hide();
        return true;
    }
    if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN &&
        (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (callbacks_.translate_now) callbacks_.translate_now();
        return true;
    }
    return IsDialogMessageW(window_, &message) != FALSE;
}

void QuickTranslateWindow::self_test(HWND owner) {
    create_window(owner);
    if (!input_ || !output_ || !target_combo_ || !source_combo_ || !swap_button_ ||
        !copy_button_ || !clear_button_ || !capture_button_ || !home_button_ ||
        SendMessageW(source_combo_, CB_GETCOUNT, 0, 0) != 9 ||
        SendMessageW(target_combo_, CB_GETCOUNT, 0, 0) != 10) {
        throw AppError("quick translation window self-test controls are incomplete");
    }
    const LONG_PTR ex_style = GetWindowLongPtrW(window_, GWL_EXSTYLE);
    if ((ex_style & WS_EX_TOOLWINDOW) == 0 || (ex_style & WS_EX_TOPMOST) == 0 ||
        (ex_style & WS_EX_APPWINDOW) != 0) {
        throw AppError("quick translation window self-test style is invalid");
    }
    if (dpi_ != static_cast<int>(GetDpiForWindow(window_))) {
        throw AppError("quick translation window self-test DPI mismatch");
    }
    RECT selector_client{};
    GetClientRect(target_combo_, &selector_client);
    HDC selector_dc = GetDC(target_combo_);
    HDC selector_test_dc = selector_dc ? CreateCompatibleDC(selector_dc) : nullptr;
    HBITMAP selector_bitmap = selector_dc
        ? CreateCompatibleBitmap(selector_dc,
                                 std::max(1L, selector_client.right),
                                 std::max(1L, selector_client.bottom))
        : nullptr;
    HGDIOBJ selector_previous = selector_test_dc && selector_bitmap
        ? SelectObject(selector_test_dc, selector_bitmap) : nullptr;
    bool closed_selector_painted = false;
    if (selector_test_dc && selector_previous) {
        RECT item_bounds = selector_client;
        item_bounds.right = std::max(item_bounds.left + 1,
                                     item_bounds.right - scaled(22, dpi_));
        DRAWITEMSTRUCT closed_item{};
        closed_item.CtlType = ODT_COMBOBOX;
        closed_item.CtlID = id_target;
        closed_item.itemID = 0;
        closed_item.itemAction = ODA_DRAWENTIRE;
        closed_item.itemState = ODS_COMBOBOXEDIT;
        closed_item.hwndItem = target_combo_;
        closed_item.hDC = selector_test_dc;
        closed_item.rcItem = item_bounds;
        draw_item(closed_item);
        closed_selector_painted =
            GetPixel(selector_test_dc, item_bounds.left, item_bounds.top) == color_input;
    }
    if (selector_previous) SelectObject(selector_test_dc, selector_previous);
    if (selector_bitmap) DeleteObject(selector_bitmap);
    if (selector_test_dc) DeleteDC(selector_test_dc);
    if (selector_dc) ReleaseDC(target_combo_, selector_dc);
    if (!closed_selector_painted) {
        throw AppError("quick translation window self-test closed target was not painted");
    }
    bool composition_started = false;
    bool composition_ended = false;
    auto composition_callback = callbacks_.composition_changed;
    callbacks_.composition_changed = [&](bool composing) {
        composition_started = composition_started || composing;
        composition_ended = composition_ended || !composing;
    };
    SendMessageW(input_, WM_IME_STARTCOMPOSITION, 0, 0);
    SendMessageW(input_, WM_IME_ENDCOMPOSITION, 0, 0);
    callbacks_.composition_changed = std::move(composition_callback);
    if (!composition_started || !composition_ended) {
        throw AppError("quick translation window self-test IME handling mismatch");
    }
    bool clear_called = false;
    auto clear_callback = callbacks_.clear;
    callbacks_.clear = [&] { clear_called = true; };
    SendMessageW(window_, WM_COMMAND, MAKEWPARAM(id_clear, BN_CLICKED),
                 reinterpret_cast<LPARAM>(clear_button_));
    callbacks_.clear = std::move(clear_callback);
    if (!clear_called || GetFocus() != input_) {
        throw AppError("quick translation window self-test clear button mismatch");
    }

    bool copy_source_called = false;
    auto copy_source_callback = callbacks_.copy;
    callbacks_.copy = [&] { copy_source_called = true; };
    SendMessageW(window_, WM_COMMAND, MAKEWPARAM(id_copy, BN_CLICKED),
                 reinterpret_cast<LPARAM>(copy_button_));
    callbacks_.copy = std::move(copy_source_callback);
    if (!copy_source_called) {
        throw AppError("quick translation window self-test copy button mismatch");
    }
    session_.set_source(L"fr");
    session_.set_target(L"ja");
    const std::wstring source_before_swap = session_.source();
    const std::wstring target_before_swap = session_.target();
    bool swap_called = false;
    std::wstring swapped_source;
    auto source_callback = callbacks_.source_changed;
    auto target_callback = callbacks_.target_changed;
    callbacks_.source_changed = [&](std::wstring value) {
        swap_called = true;
        swapped_source = value;
        session_.set_source(std::move(value));
    };
    callbacks_.target_changed = [&](std::wstring value) {
        session_.set_target(std::move(value));
    };
    SendMessageW(window_, WM_COMMAND, MAKEWPARAM(id_swap, BN_CLICKED),
                 reinterpret_cast<LPARAM>(swap_button_));
    callbacks_.source_changed = std::move(source_callback);
    callbacks_.target_changed = std::move(target_callback);
    if (!swap_called || swapped_source != target_before_swap ||
        session_.target() != source_before_swap || source_before_swap == target_before_swap) {
        throw AppError("quick translation window self-test swap mismatch");
    }
    bool capture_called = false;
    auto capture_callback = callbacks_.capture;
    callbacks_.capture = [&] { capture_called = true; };
    SendMessageW(window_, WM_COMMAND, MAKEWPARAM(id_quick_capture, BN_CLICKED),
                 reinterpret_cast<LPARAM>(capture_button_));
    callbacks_.capture = std::move(capture_callback);
    if (!capture_called || visible()) {
        throw AppError("quick translation window self-test capture button mismatch");
    }
    RECT capture_rect{};
    RECT home_rect{};
    RECT close_rect{};
    RECT source_combo_rect{};
    if (!GetWindowRect(capture_button_, &capture_rect) ||
        !GetWindowRect(home_button_, &home_rect) ||
        !GetWindowRect(close_button_, &close_rect) ||
        !GetWindowRect(source_combo_, &source_combo_rect)) {
        throw AppError("quick translation window self-test header buttons missing");
    }
    if (close_rect.right - close_rect.left != capture_rect.right - capture_rect.left ||
        close_rect.bottom - close_rect.top != capture_rect.bottom - capture_rect.top ||
        close_rect.top != capture_rect.top || close_rect.left <= home_rect.right ||
        home_rect.left <= capture_rect.right ||
        capture_rect.left <= source_combo_rect.right ||
        close_rect.right > source_combo_rect.left + scaled(480, dpi_)) {
        throw AppError("quick translation window self-test header layout mismatch");
    }
    show(owner);
    bool home_called = false;
    auto home_callback = callbacks_.home;
    callbacks_.home = [&] { home_called = true; };
    SendMessageW(window_, WM_COMMAND, MAKEWPARAM(id_quick_home, BN_CLICKED),
                 reinterpret_cast<LPARAM>(home_button_));
    callbacks_.home = std::move(home_callback);
    if (!home_called) {
        throw AppError("quick translation window self-test home button mismatch");
    }
    SendMessageW(window_, WM_COMMAND, MAKEWPARAM(id_quick_close, BN_CLICKED),
                 reinterpret_cast<LPARAM>(close_button_));
    if (visible()) {
        throw AppError("quick translation window self-test close button mismatch");
    }
    RECT copy_button_rect{};
    RECT clear_button_rect{};
    RECT quick_input_rect{};
    RECT quick_output_rect{};
    if (!GetWindowRect(copy_button_, &copy_button_rect) ||
        !GetWindowRect(clear_button_, &clear_button_rect) ||
        !GetWindowRect(input_, &quick_input_rect) ||
        !GetWindowRect(output_, &quick_output_rect)) {
        throw AppError("quick translation window self-test action buttons missing");
    }
    const int action_width = clear_button_rect.right - clear_button_rect.left;
    const int action_height = clear_button_rect.bottom - clear_button_rect.top;
    const int label_row_height = scaled(29, dpi_);
    if (clear_button_rect.left != copy_button_rect.left ||
        copy_button_rect.right - copy_button_rect.left != action_width ||
        copy_button_rect.bottom - copy_button_rect.top != action_height ||
        copy_button_rect.top <= clear_button_rect.bottom ||
        clear_button_rect.bottom > quick_input_rect.top ||
        clear_button_rect.top < quick_input_rect.top - label_row_height ||
        copy_button_rect.top < quick_input_rect.bottom ||
        copy_button_rect.bottom > quick_output_rect.top ||
        clear_button_rect.left < quick_input_rect.left ||
        clear_button_rect.left >= quick_input_rect.left + scaled(80, dpi_) ||
        copy_button_rect.right > quick_input_rect.right) {
        throw AppError("quick translation window self-test action buttons differ");
    }
    show(owner);
    RECT quick_client{};
    if (!GetClientRect(window_, &quick_client) ||
        quick_client.right < scaled(quick_window_logical_width, dpi_) / 2 ||
        quick_client.bottom < scaled(quick_window_logical_height, dpi_) / 2) {
        throw AppError("quick translation window self-test size mismatch");
    }
    RECT target_before_dropdown{};
    if (!GetWindowRect(target_combo_, &target_before_dropdown)) {
        throw AppError("quick translation window self-test target selector missing");
    }
    SendMessageW(target_combo_, CB_SHOWDROPDOWN, TRUE, 0);
    if (!SendMessageW(target_combo_, CB_GETDROPPEDSTATE, 0, 0)) {
        throw AppError("quick translation window self-test target menu did not open");
    }
    COMBOBOXINFO target_info{sizeof(target_info)};
    RECT target_list{};
    if (!GetComboBoxInfo(target_combo_, &target_info) || !target_info.hwndList ||
        !GetWindowRect(target_info.hwndList, &target_list) ||
        target_list.bottom - target_list.top < scaled(200, dpi_)) {
        throw AppError("quick translation window self-test target menu is clipped");
    }
    SendMessageW(target_combo_, CB_SHOWDROPDOWN, FALSE, 0);
    RECT target_after_dropdown{};
    if (!GetWindowRect(target_combo_, &target_after_dropdown) ||
        target_after_dropdown.left != target_before_dropdown.left ||
        target_after_dropdown.top != target_before_dropdown.top ||
        target_after_dropdown.right <= target_after_dropdown.left ||
        target_after_dropdown.bottom <= target_after_dropdown.top) {
        throw AppError("quick translation window self-test target selector resized");
    }
    SendMessageW(window_, WM_CLOSE, 0, 0);
    if (visible()) {
        throw AppError("quick translation window self-test close path did not hide");
    }
    show(owner);
    // The language selectors now sit on the left edge, so the draggable header
    // area is the gap between the last selector and the right-side buttons.
    RECT header_target{};
    RECT header_capture{};
    GetWindowRect(target_combo_, &header_target);
    GetWindowRect(capture_button_, &header_capture);
    const POINT drag_point{(header_target.right + header_capture.left) / 2,
                           (header_target.top + header_target.bottom) / 2};
    const auto drag_hit = SendMessageW(
        window_, WM_NCHITTEST, 0,
        MAKELPARAM(static_cast<short>(drag_point.x),
                   static_cast<short>(drag_point.y)));
    RECT input_bounds{};
    GetWindowRect(input_, &input_bounds);
    const POINT input_point{
        (input_bounds.left + input_bounds.right) / 2,
        (input_bounds.top + input_bounds.bottom) / 2,
    };
    const auto input_hit = SendMessageW(
        window_, WM_NCHITTEST, 0,
        MAKELPARAM(static_cast<short>(input_point.x),
                   static_cast<short>(input_point.y)));
    if (drag_hit != HTCAPTION || input_hit != HTCLIENT) {
        throw AppError("quick translation window self-test drag regions mismatch");
    }
    hide();
    for (int index = 0; index < 4; ++index) {
        show(owner);
        UpdateWindow(window_);
        UpdateWindow(target_combo_);
        hide();
    }
    settle_self_test_ui();
    std::array<ProcessResourceSnapshot, 9> resource_samples{};
    resource_samples[0] = process_resources();
    for (std::size_t batch = 1; batch < resource_samples.size(); ++batch) {
        for (int index = 0; index < 24; ++index) {
            show(owner);
            UpdateWindow(window_);
            UpdateWindow(target_combo_);
            hide();
        }
        resource_samples[batch] = process_resources();
    }
    if (!resource_series_stable(resource_samples)) {
        throw AppError("quick translation window self-test leaked process resources" +
                       resource_change(resource_samples.front(),
                                       resource_samples.back()));
    }
    hide();
}

LRESULT CALLBACK QuickTranslateWindow::input_proc(HWND control, UINT message,
                                                  WPARAM wparam, LPARAM lparam,
                                                  UINT_PTR subclass_id, DWORD_PTR reference) {
    auto* self = reinterpret_cast<QuickTranslateWindow*>(reference);
    if (self && message == WM_IME_STARTCOMPOSITION && self->callbacks_.composition_changed) {
        self->callbacks_.composition_changed(true);
    } else if (self && message == WM_IME_ENDCOMPOSITION && self->callbacks_.composition_changed) {
        self->callbacks_.composition_changed(false);
    }
    if (self && (message == WM_SETFOCUS || message == WM_KILLFOCUS)) {
        InvalidateRect(self->window_, nullptr, FALSE);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(control, &QuickTranslateWindow::input_proc,
                                                       subclass_id);
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT CALLBACK QuickTranslateWindow::target_combo_proc(
        HWND control, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR subclass_id, DWORD_PTR reference) {
    auto* self = reinterpret_cast<QuickTranslateWindow*>(reference);
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(control, &paint);
        if (self) {
            if (subclass_id == source_combo_subclass_id) {
                self->draw_source_combo(dc);
            } else {
                self->draw_target_combo(dc);
            }
        }
        EndPaint(control, &paint);
        return 0;
    }
    case WM_PRINTCLIENT:
        if (self) {
            if (subclass_id == source_combo_subclass_id) {
                self->draw_source_combo(reinterpret_cast<HDC>(wparam));
            } else {
                self->draw_target_combo(reinterpret_cast<HDC>(wparam));
            }
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        RedrawWindow(control, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, &QuickTranslateWindow::target_combo_proc,
                             subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT CALLBACK QuickTranslateWindow::window_proc(HWND window, UINT message,
                                                   WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<QuickTranslateWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<QuickTranslateWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wparam, lparam);
    return self->handle_message(message, wparam, lparam);
}

LRESULT QuickTranslateWindow::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case id_input:
            if (HIWORD(wparam) == EN_CHANGE) {
                InvalidateRect(input_, nullptr, FALSE);
                on_input_changed();
            }
            if (HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS) {
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        case id_output:
            if (HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS) {
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        case id_source:
            if (HIWORD(wparam) == CBN_DROPDOWN) {
                theme::apply_control_theme(source_combo_);
                style_target_popup(source_combo_, dpi_, false);
            }
            if (HIWORD(wparam) == CBN_SELCHANGE) on_source_changed();
            return 0;
        case id_target:
            if (HIWORD(wparam) == CBN_DROPDOWN) {
                theme::apply_control_theme(target_combo_);
                style_target_popup(target_combo_, dpi_, false);
            }
            if (HIWORD(wparam) == CBN_SELCHANGE) on_target_changed();
            return 0;
        case id_swap:
            if (HIWORD(wparam) == BN_CLICKED) swap_languages();
            return 0;
        case id_copy:
            if (HIWORD(wparam) == BN_CLICKED && callbacks_.copy) {
                callbacks_.copy();
            }
            return 0;
        case id_clear:
            if (HIWORD(wparam) == BN_CLICKED) {
                if (callbacks_.clear) callbacks_.clear();
                SetFocus(input_);
            }
            return 0;
        case id_quick_capture:
            if (HIWORD(wparam) == BN_CLICKED) {
                hide();
                if (callbacks_.capture) callbacks_.capture();
            }
            return 0;
        case id_quick_home:
            if (HIWORD(wparam) == BN_CLICKED && callbacks_.home) {
                callbacks_.home();
            }
            return 0;
        case id_quick_close:
            if (HIWORD(wparam) == BN_CLICKED) hide();
            return 0;
        default: break;
        }
        break;
    case WM_SIZE: layout_controls(); return 0;
    case WM_DPICHANGED:
        dpi_ = HIWORD(wparam);
        if (const auto* suggested = reinterpret_cast<const RECT*>(lparam)) {
            SetWindowPos(window_, HWND_TOPMOST, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOACTIVATE);
        }
        refresh_appearance();
        layout_controls();
        return 0;
    case theme::changed_message:
        refresh_appearance();
        return 0;
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(window_, message, wparam, lparam);
        if (hit != HTCLIENT) return hit;
        const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (point_in_window(input_, point) || point_in_window(output_, point) ||
            point_in_window(source_combo_, point) ||
            point_in_window(swap_button_, point) ||
            point_in_window(target_combo_, point) ||
            point_in_window(copy_button_, point) ||
            point_in_window(clear_button_, point) ||
            point_in_window(capture_button_, point) ||
            point_in_window(home_button_, point)) {
            return HTCLIENT;
        }
        return HTCAPTION;
    }
    case WM_DRAWITEM:
        if (lparam) return draw_item(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
        break;
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, color_text);
        SetBkColor(dc, color_surface);
        return reinterpret_cast<LRESULT>(input_background_);
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, color_text);
        SetBkColor(dc, color_surface);
        return reinterpret_cast<LRESULT>(input_background_);
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(); return 0;
    case WM_CLOSE: hide(); return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        if (output_) {
            RemoveWindowSubclass(output_, &keep_editor_unfocused,
                                 output_editor_subclass_id);
        }
        window_ = nullptr;
        source_combo_ = swap_button_ = nullptr;
        target_combo_ = input_ = output_ = nullptr;
        capture_button_ = home_button_ = close_button_ = nullptr;
        copy_button_ = clear_button_ = nullptr;
        return 0;
    default: break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace minitrans
