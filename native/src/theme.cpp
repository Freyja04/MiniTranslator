#include "theme.hpp"
#include "resource.h"

#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <cstdint>
#include <cstring>
namespace minitrans::theme {

namespace {

constexpr DWORD immersive_dark_mode = 20;
constexpr DWORD border_color = 34;
constexpr DWORD caption_color = 35;
constexpr DWORD caption_text_color = 36;
constexpr DWORD window_corner_preference = 33;

// Counts the palette rewrites that windows cache against: fonts, brushes and
// owner-drawn frames are only stale once these values actually moved.
std::uint64_t visual_revision_counter = 0;

std::wstring system_ui_font_family() {
    NONCLIENTMETRICSW metrics{sizeof(metrics)};
    if (SystemParametersInfoW(SPI_GETNONCLIENTMETRICS, sizeof(metrics),
                              &metrics, 0) &&
        metrics.lfMessageFont.lfFaceName[0] != L'\0') {
        return metrics.lfMessageFont.lfFaceName;
    }
    return L"Segoe UI";
}

bool read_personalize_dword(const wchar_t* name, DWORD& value) noexcept {
    DWORD size = sizeof(value);
    return RegGetValueW(
               HKEY_CURRENT_USER,
               L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
               name, RRF_RT_REG_DWORD, nullptr, &value, &size) == ERROR_SUCCESS;
}

void apply_dark_palette() noexcept {
    color_page = RGB(21, 22, 27);
    color_sidebar = RGB(16, 17, 22);
    color_card = RGB(27, 29, 35);
    color_input = RGB(33, 36, 41);
    color_translation = RGB(25, 27, 32);
    color_line = RGB(40, 43, 51);
    color_line_high = RGB(52, 56, 66);
    color_hover = RGB(45, 48, 56);
    color_nav_hover = RGB(34, 36, 42);
    color_nav_active = RGB(39, 42, 49);
    color_text = RGB(231, 233, 236);
    color_text_dim = RGB(148, 154, 164);
    color_text_faint = RGB(110, 116, 126);
    color_bad = RGB(255, 107, 107);
    color_ok = RGB(78, 209, 139);
    color_warn = RGB(232, 180, 74);
    accent = RGB(40, 199, 111);
    accent_foreground = RGB(14, 16, 19);

    tray_color_background = RGB(32, 32, 32);
    tray_color_separator = RGB(61, 61, 61);
    tray_color_hover = RGB(48, 48, 48);
    tray_color_text = RGB(243, 243, 243);
    tray_color_text_dim = RGB(178, 178, 178);
    tray_color_text_faint = RGB(112, 112, 112);
}

void apply_light_palette() noexcept {
    color_page = RGB(244, 246, 248);
    color_sidebar = RGB(236, 238, 242);
    color_card = RGB(255, 255, 255);
    color_input = RGB(247, 248, 250);
    color_translation = RGB(242, 245, 247);
    color_line = RGB(217, 222, 230);
    color_line_high = RGB(197, 204, 214);
    color_hover = RGB(232, 235, 240);
    color_nav_hover = RGB(224, 228, 234);
    color_nav_active = RGB(218, 224, 232);
    color_text = RGB(30, 35, 42);
    color_text_dim = RGB(94, 103, 116);
    color_text_faint = RGB(138, 147, 160);
    color_bad = RGB(201, 60, 60);
    color_ok = RGB(21, 159, 99);
    color_warn = RGB(183, 121, 31);
    accent = RGB(21, 159, 99);
    accent_foreground = RGB(255, 255, 255);

    tray_color_background = RGB(255, 255, 255);
    tray_color_separator = RGB(229, 232, 237);
    tray_color_hover = RGB(232, 235, 240);
    tray_color_text = RGB(30, 35, 42);
    tray_color_text_dim = RGB(94, 103, 116);
    tray_color_text_faint = RGB(138, 147, 160);
}

void update_aliases() noexcept {
    color_background = color_page;
    color_surface = color_card;
    color_border = color_line;
    color_dim = color_text_dim;
    color_faint = color_text_faint;
    color_error = color_bad;
    color_warning = color_warn;
}

}  // namespace

bool system_uses_dark_mode() noexcept {
    DWORD apps_use_light = 1;
    if (read_personalize_dword(L"AppsUseLightTheme", apps_use_light)) {
        return apps_use_light == 0;
    }
    return false;
}

std::wstring mode_name(Mode value) {
    switch (value) {
    case Mode::light: return L"light";
    case Mode::dark: return L"dark";
    case Mode::system: break;
    }
    return L"system";
}

Mode parse_mode(std::wstring_view value) noexcept {
    const std::wstring text(value);
    if (_wcsicmp(text.c_str(), L"light") == 0) return Mode::light;
    if (_wcsicmp(text.c_str(), L"dark") == 0) return Mode::dark;
    return Mode::system;
}

std::uint64_t visual_revision() noexcept {
    return visual_revision_counter;
}

void initialize(const ConfigStore& config) {
    mode = parse_mode(config.string(L"appearance.theme", L"system"));
    font_family = system_ui_font_family();
    apply_mode(mode);
}

void refresh_system_ui_font() {
    const std::wstring updated = system_ui_font_family();
    if (updated == font_family) return;
    font_family = updated;
    ++visual_revision_counter;
}

void apply_mode(Mode value) noexcept {
    const bool was_dark = dark;
    mode = value;
    dark = value == Mode::dark ||
           (value == Mode::system && system_uses_dark_mode());
    if (dark) {
        apply_dark_palette();
    } else {
        apply_light_palette();
    }
    update_aliases();
    if (dark != was_dark) ++visual_revision_counter;
}

void refresh_system_mode() noexcept {
    if (mode == Mode::system) apply_mode(Mode::system);
}

void apply_window_theme(HWND window, bool round_corners, bool custom_caption) noexcept {
    if (!window) return;
    SetWindowTheme(window, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
    BOOL enabled = dark ? TRUE : FALSE;
    DwmSetWindowAttribute(window, immersive_dark_mode, &enabled, sizeof(enabled));
    if (round_corners) {
        constexpr int round = 2;
        DwmSetWindowAttribute(window, window_corner_preference, &round, sizeof(round));
    }
    const COLORREF border = color_line_high;
    DwmSetWindowAttribute(window, border_color, &border, sizeof(border));
    if (custom_caption) {
        const COLORREF caption = color_page;
        const COLORREF text = color_text;
        DwmSetWindowAttribute(window, caption_color, &caption, sizeof(caption));
        DwmSetWindowAttribute(window, caption_text_color, &text, sizeof(text));
    } else {
        const COLORREF none = 0xFFFFFFFF;
        DwmSetWindowAttribute(window, caption_color, &none, sizeof(none));
        DwmSetWindowAttribute(window, caption_text_color, &none, sizeof(none));
    }

    // Windows 10 keeps the old DWM frame after a dynamic theme change. A
    // non-client invalidation or SWP_FRAMECHANGED alone can be ignored, so
    // resize the frame by one pixel and restore it to force DWM to rebuild.
    const LONG_PTR style = GetWindowLongPtrW(window, GWL_STYLE);
    if ((style & WS_CAPTION) == WS_CAPTION &&
        GetAncestor(window, GA_ROOT) == window) {
        RECT bounds{};
        if (!IsIconic(window) && !IsZoomed(window) &&
            GetWindowRect(window, &bounds) &&
            bounds.right - bounds.left > 2) {
            const int width = bounds.right - bounds.left;
            const int height = bounds.bottom - bounds.top;
            SetWindowPos(window, nullptr, 0, 0, width - 1, height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE |
                             SWP_NOOWNERZORDER);
            SetWindowPos(window, nullptr, 0, 0, width, height,
                         SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE |
                             SWP_NOOWNERZORDER);
        } else {
            SetWindowPos(window, nullptr, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                             SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
        }
        RedrawWindow(window, nullptr, nullptr,
                     RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
        if (IsWindowVisible(window)) {
            const bool active = GetActiveWindow() == window;
            SendMessageW(window, WM_NCACTIVATE, active ? FALSE : TRUE, 0);
            SendMessageW(window, WM_NCACTIVATE, active ? TRUE : FALSE, 0);
        }
        DwmFlush();
    }
}

void apply_control_theme(HWND control) noexcept {
    if (control) SetWindowTheme(control, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

void draw_brand_mark(HDC dc, const RECT& bounds, COLORREF color) noexcept {
    const int width = static_cast<int>(bounds.right - bounds.left);
    const int height = static_cast<int>(bounds.bottom - bounds.top);
    if (!dc || width <= 0 || height <= 0) return;
    // A 16 px version of the application mark: corner brackets plus a bold "A".
    // Downscaling the bitmap icon to menu size turned it into a blur, so the
    // mark is drawn as vectors in the same color as the neighbouring icons.
    const auto map_x = [&](int logical) {
        return bounds.left + MulDiv(logical, width, 16);
    };
    const auto map_y = [&](int logical) {
        return bounds.top + MulDiv(logical, height, 16);
    };
    const int stroke = std::max(1, height / 8);
    HPEN pen = CreatePen(PS_SOLID, stroke, color);
    if (!pen) return;
    const HGDIOBJ previous_pen = SelectObject(dc, pen);
    const auto line = [&](int x0, int y0, int x1, int y1) {
        MoveToEx(dc, map_x(x0), map_y(y0), nullptr);
        LineTo(dc, map_x(x1), map_y(y1));
    };
    constexpr int inset = 1;
    constexpr int far_edge = 14;
    constexpr int arm = 3;
    line(inset, inset, inset + arm, inset);
    line(inset, inset, inset, inset + arm);
    line(far_edge, inset, far_edge - arm, inset);
    line(far_edge, inset, far_edge, inset + arm);
    line(inset, far_edge, inset + arm, far_edge);
    line(inset, far_edge, inset, far_edge - arm);
    line(far_edge, far_edge, far_edge - arm, far_edge);
    line(far_edge, far_edge, far_edge, far_edge - arm);
    SelectObject(dc, previous_pen);
    DeleteObject(pen);

    const int font_height = std::max(7, MulDiv(height, 11, 16));
    HFONT font = CreateFontW(-font_height, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                             DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                             CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                             L"Segoe UI");
    if (!font) return;
    const HGDIOBJ previous_font = SelectObject(dc, font);
    const COLORREF previous_color = SetTextColor(dc, color);
    const int previous_mode = SetBkMode(dc, TRANSPARENT);
    RECT text_bounds{map_x(3), map_y(2), map_x(13), map_y(13)};
    DrawTextW(dc, L"A", 1, &text_bounds,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SetBkMode(dc, previous_mode);
    SetTextColor(dc, previous_color);
    SelectObject(dc, previous_font);
    DeleteObject(font);
}

}  // namespace minitrans::theme
