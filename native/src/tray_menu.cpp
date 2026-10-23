#include "tray_menu.hpp"

#include "resource.h"
#include "theme.hpp"
#include "ui_icons.hpp"
#include "util.hpp"

#include <shellapi.h>
#include <shellscalingapi.h>

#include <algorithm>
#include <array>
#include <cwchar>

namespace minitrans::tray_menu {

// The self test builds sample items; the command identifiers only need to be
// distinct, so the menu module keeps its own local copy.
constexpr UINT command_capture = 3001;
constexpr UINT command_restore = 3002;
constexpr UINT command_settings = 3003;
constexpr UINT command_exit = 3006;
constexpr UINT command_text_translate = 3007;

using theme::tray_color_background;
using theme::tray_color_hover;
using theme::tray_color_separator;
using theme::tray_color_text;
using theme::tray_color_text_dim;
using theme::tray_color_text_faint;
constexpr int tray_menu_min_width = 240;
constexpr int tray_menu_row_height = 32;
constexpr int tray_menu_edge_padding = 4;
constexpr int tray_menu_icon_size = 16;
constexpr std::uint32_t item_magic = 0x53544D49;  // STMI

int scaled(int logical, UINT dpi) noexcept {
    return std::max(1, MulDiv(logical, static_cast<int>(dpi), 96));
}

UINT menu_dpi(POINT location) noexcept {
    const HMONITOR monitor = MonitorFromPoint(location, MONITOR_DEFAULTTONEAREST);
    UINT dpi_x = 96;
    UINT dpi_y = 96;
    if (monitor && SUCCEEDED(GetDpiForMonitor(
            monitor, MDT_EFFECTIVE_DPI, &dpi_x, &dpi_y))) {
        return std::max<UINT>(96, dpi_x);
    }
    return std::max<UINT>(96, GetDpiForSystem());
}

HFONT create_font(UINT dpi) noexcept {
    return CreateFontW(-MulDiv(9, static_cast<int>(dpi), 72), 0, 0, 0, FW_NORMAL,
                       FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                       CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, theme::font_family.c_str());
}

void CALLBACK style_menu_window(HWINEVENTHOOK, DWORD event, HWND window,
                                     LONG, LONG, DWORD, DWORD) noexcept {
    if (event != EVENT_SYSTEM_MENUPOPUPSTART || !window) return;
    wchar_t class_name[16]{};
    if (!GetClassNameW(window, class_name, static_cast<int>(std::size(class_name))) ||
        wcscmp(class_name, L"#32768") != 0) {
        return;
    }

    theme::apply_window_theme(window);
}

bool is_menu_item(ULONG_PTR data) noexcept {
    const auto* item = reinterpret_cast<const Item*>(data);
    return item && item->magic == item_magic;
}

int CALLBACK find_tray_icon_font(const LOGFONTW*, const TEXTMETRICW*, DWORD,
                                 LPARAM data) {
    *reinterpret_cast<bool*>(data) = true;
    return 0;
}

bool tray_icon_font_available(HDC dc, const wchar_t* face) {
    LOGFONTW query{};
    query.lfCharSet = DEFAULT_CHARSET;
    if (wcsncpy_s(query.lfFaceName, LF_FACESIZE, face, _TRUNCATE) != 0) {
        return false;
    }
    bool found = false;
    EnumFontFamiliesExW(dc, &query, find_tray_icon_font,
                        reinterpret_cast<LPARAM>(&found), 0);
    return found;
}

const wchar_t* tray_icon_font(HDC dc) {
    static int fluent_available = -1;
    if (fluent_available < 0) {
        fluent_available = tray_icon_font_available(dc, L"Segoe Fluent Icons") ? 1 : 0;
    }
    return fluent_available ? L"Segoe Fluent Icons" : L"Segoe MDL2 Assets";
}

// Use the real tray icon position for both entry points so the panel has one
// stable anchor even when the hotkey fires while the cursor is elsewhere.
POINT anchor_for_quick_translation(HWND owner, UINT icon_id) noexcept {
    NOTIFYICONIDENTIFIER identifier{};
    identifier.cbSize = sizeof(identifier);
    identifier.hWnd = owner;
    identifier.uID = icon_id;
    RECT icon{};
    if (SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &icon))) {
        return POINT{(icon.left + icon.right) / 2, (icon.top + icon.bottom) / 2};
    }
    const HMONITOR monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO info{sizeof(info)};
    if (monitor && GetMonitorInfoW(monitor, &info)) {
        return POINT{info.rcWork.right, info.rcWork.bottom};
    }
    return POINT{GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
}

void draw_icon(HDC dc, const RECT& bounds, Icon icon,
                         COLORREF color, UINT dpi) {
    if (icon == Icon::none) return;
    // The text translation entry leads with the application mark instead of a
    // generic glyph; it is redrawn in the menu foreground color so it matches
    // the neighbouring icons.
    if (icon == Icon::brand) {
        theme::draw_brand_mark(dc, bounds, color);
        return;
    }
    wchar_t glyph = 0;
    switch (icon) {
    case Icon::check:
        glyph = static_cast<wchar_t>(0xE73E);  // CheckMark
        break;
    case Icon::capture:
        glyph = ui_icons::capture_translation;
        break;
    case Icon::text:
        glyph = ui_icons::text_translation;
        break;
    case Icon::restore:
        glyph = static_cast<wchar_t>(0xE890);  // View
        break;
    case Icon::home:
        glyph = static_cast<wchar_t>(0xE80F);  // Home
        break;
    case Icon::exit:
        glyph = static_cast<wchar_t>(0xE7E8);  // PowerButton
        break;
    case Icon::none:
        break;
    case Icon::brand:
        break;
    }
    if (!glyph) return;

    const int icon_width = static_cast<int>(bounds.right - bounds.left);
    const int icon_height = static_cast<int>(bounds.bottom - bounds.top);
    const int font_pixels = std::max(scaled(14, dpi),
                                     std::min(icon_width, icon_height));
    HFONT font = CreateFontW(-font_pixels, 0, 0, 0, FW_NORMAL,
                             FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             tray_icon_font(dc));
    if (!font) return;

    const int saved = SaveDC(dc);
    SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    RECT text_bounds = bounds;
    DrawTextW(dc, &glyph, 1, &text_bounds,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    RestoreDC(dc, saved);
    DeleteObject(font);
}

bool measure_item(MEASUREITEMSTRUCT& measure) {
    if (measure.CtlType != ODT_MENU || !is_menu_item(measure.itemData)) return false;
    const auto& item = *reinterpret_cast<const Item*>(measure.itemData);
    const int top_padding = item.first ? tray_menu_edge_padding : 0;
    const int bottom_padding = item.last ? tray_menu_edge_padding : 0;
    if (item.separator) {
        measure.itemWidth = static_cast<UINT>(scaled(tray_menu_min_width, item.dpi));
        measure.itemHeight = static_cast<UINT>(scaled(9 + top_padding + bottom_padding,
                                                           item.dpi));
        return true;
    }

    SIZE label_size{};
    SIZE secondary_size{};
    HDC dc = GetDC(nullptr);
    HGDIOBJ previous_font = nullptr;
    if (dc && item.font) previous_font = SelectObject(dc, item.font);
    if (dc) {
        GetTextExtentPoint32W(dc, item.label.c_str(), static_cast<int>(item.label.size()),
                              &label_size);
        GetTextExtentPoint32W(dc, item.secondary.c_str(),
                              static_cast<int>(item.secondary.size()), &secondary_size);
        if (previous_font) SelectObject(dc, previous_font);
        ReleaseDC(nullptr, dc);
    }

    const int secondary_gap = item.secondary.empty() ? 0 : scaled(22, item.dpi);
    const int measured_width = scaled(54, item.dpi) + label_size.cx +
                               secondary_gap + secondary_size.cx + scaled(14, item.dpi);
    measure.itemWidth = static_cast<UINT>(std::max(
        scaled(tray_menu_min_width, item.dpi), measured_width));
    measure.itemHeight = static_cast<UINT>(scaled(
        tray_menu_row_height + top_padding + bottom_padding,
                                                       item.dpi));
    return true;
}

bool draw_item(const DRAWITEMSTRUCT& draw) {
    if (draw.CtlType != ODT_MENU || !is_menu_item(draw.itemData)) return false;
    const auto& item = *reinterpret_cast<const Item*>(draw.itemData);
    const bool disabled = !item.enabled || (draw.itemState & ODS_DISABLED) != 0;
    const bool selected = !disabled && (draw.itemState & ODS_SELECTED) != 0;

    HBRUSH background = CreateSolidBrush(tray_color_background);
    if (background) {
        FillRect(draw.hDC, &draw.rcItem, background);
        DeleteObject(background);
    }

    RECT content = draw.rcItem;
    if (item.first) content.top += scaled(tray_menu_edge_padding, item.dpi);
    if (item.last) content.bottom -= scaled(tray_menu_edge_padding, item.dpi);

    if (item.separator) {
        const int middle = content.top + (content.bottom - content.top) / 2;
        HPEN pen = CreatePen(PS_SOLID, scaled(1, item.dpi), tray_color_separator);
        if (pen) {
            const auto previous = SelectObject(draw.hDC, pen);
            MoveToEx(draw.hDC, content.left + scaled(14, item.dpi), middle, nullptr);
            LineTo(draw.hDC, content.right - scaled(14, item.dpi), middle);
            SelectObject(draw.hDC, previous);
            DeleteObject(pen);
        }
        return true;
    }

    if (selected) {
        RECT highlight = content;
        InflateRect(&highlight, -scaled(3, item.dpi), -scaled(1, item.dpi));
        HBRUSH accent_brush = CreateSolidBrush(tray_color_hover);
        HPEN accent_pen = CreatePen(PS_SOLID, 1, tray_color_hover);
        if (accent_brush && accent_pen) {
            const auto old_brush = SelectObject(draw.hDC, accent_brush);
            const auto old_pen = SelectObject(draw.hDC, accent_pen);
            RoundRect(draw.hDC, highlight.left, highlight.top, highlight.right, highlight.bottom,
                      scaled(8, item.dpi), scaled(8, item.dpi));
            SelectObject(draw.hDC, old_pen);
            SelectObject(draw.hDC, old_brush);
        }
        if (accent_pen) DeleteObject(accent_pen);
        if (accent_brush) DeleteObject(accent_brush);
    }

    const COLORREF foreground = selected ? theme::color_text
        : disabled ? tray_color_text_faint : tray_color_text;
    const COLORREF secondary_color = selected ? theme::color_text_dim
        : disabled ? tray_color_text_faint : tray_color_text_dim;
    const int icon_size = scaled(tray_menu_icon_size, item.dpi);
    RECT icon_bounds{
        content.left + scaled(14, item.dpi),
        content.top + (content.bottom - content.top - icon_size) / 2,
        content.left + scaled(14, item.dpi) + icon_size,
        content.top + (content.bottom - content.top + icon_size) / 2,
    };
    const auto icon = (draw.itemState & ODS_CHECKED) != 0
        ? Icon::check : item.icon;
    draw_icon(draw.hDC, icon_bounds, icon,
                        foreground, item.dpi);

    const int saved = SaveDC(draw.hDC);
    SetBkMode(draw.hDC, TRANSPARENT);
    if (item.font) SelectObject(draw.hDC, item.font);

    RECT label_rect = content;
    label_rect.left += scaled(43, item.dpi);
    label_rect.right -= scaled(14, item.dpi);
    if (!item.secondary.empty()) {
        SIZE secondary_size{};
        GetTextExtentPoint32W(draw.hDC, item.secondary.c_str(),
                              static_cast<int>(item.secondary.size()), &secondary_size);
        label_rect.right -= secondary_size.cx + scaled(22, item.dpi);
        RECT secondary_rect = content;
        secondary_rect.left = label_rect.right + scaled(12, item.dpi);
        secondary_rect.right -= scaled(14, item.dpi);
        SetTextColor(draw.hDC, secondary_color);
        DrawTextW(draw.hDC, item.secondary.c_str(), static_cast<int>(item.secondary.size()),
                  &secondary_rect, DT_SINGLELINE | DT_VCENTER | DT_RIGHT | DT_NOPREFIX);
    }
    SetTextColor(draw.hDC, foreground);
    DrawTextW(draw.hDC, item.label.c_str(), static_cast<int>(item.label.size()), &label_rect,
              DT_SINGLELINE | DT_VCENTER | DT_LEFT | DT_END_ELLIPSIS | DT_NOPREFIX);
    RestoreDC(draw.hDC, saved);
    return true;
}

void layout_self_test() {
    static_assert(tray_menu_icon_size <= tray_menu_row_height / 2);
    Item item;
    item.command = command_capture;
    item.icon = Icon::capture;
    item.label = L"截图翻译";
    item.secondary = L"Ctrl+Alt+Q";
    item.dpi = 96;
    item.font = create_font(item.dpi);
    item.first = true;

    MEASUREITEMSTRUCT measured{};
    measured.CtlType = ODT_MENU;
    measured.itemData = reinterpret_cast<ULONG_PTR>(&item);
    if (!measure_item(measured) ||
        measured.itemHeight != static_cast<UINT>(scaled(
            tray_menu_row_height + tray_menu_edge_padding, item.dpi)) ||
        measured.itemWidth < static_cast<UINT>(
            scaled(tray_menu_min_width, item.dpi))) {
        if (item.font) DeleteObject(item.font);
        throw AppError("tray menu layout self-test failed");
    }

    HDC screen_dc = GetDC(nullptr);
    HDC dc = CreateCompatibleDC(screen_dc);
    HBITMAP bitmap = dc && screen_dc ? CreateCompatibleBitmap(
        screen_dc, static_cast<int>(measured.itemWidth),
        static_cast<int>(measured.itemHeight)) : nullptr;
    if (screen_dc) ReleaseDC(nullptr, screen_dc);
    HGDIOBJ previous_bitmap = bitmap && dc ? SelectObject(dc, bitmap) : nullptr;
    DRAWITEMSTRUCT drawn{};
    drawn.CtlType = ODT_MENU;
    drawn.itemState = ODS_SELECTED;
    drawn.hDC = dc;
    drawn.rcItem = {0, 0, static_cast<LONG>(measured.itemWidth),
                    static_cast<LONG>(measured.itemHeight)};
    drawn.itemData = reinterpret_cast<ULONG_PTR>(&item);
    const bool rendered = dc && bitmap && draw_item(drawn);
    if (previous_bitmap) SelectObject(dc, previous_bitmap);
    if (bitmap) DeleteObject(bitmap);
    if (dc) DeleteDC(dc);
    if (item.font) DeleteObject(item.font);
    if (!rendered) throw AppError("tray menu render self-test failed");
}

HICON load_app_icon(HINSTANCE instance) noexcept {
    return static_cast<HICON>(LoadImageW(
        instance, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON,
        32, 32, LR_DEFAULTCOLOR));
}


}  // namespace minitrans::tray_menu
