#include "theme.hpp"

#include <dwmapi.h>
#include <uxtheme.h>

#include <algorithm>
namespace screentrans::theme {

namespace {

constexpr DWORD immersive_dark_mode = 20;
constexpr DWORD border_color = 34;
constexpr DWORD caption_color = 35;
constexpr DWORD caption_text_color = 36;
constexpr DWORD window_corner_preference = 33;

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
    color_text = RGB(231, 233, 236);
    color_text_dim = RGB(148, 154, 164);
    color_text_faint = RGB(110, 116, 126);
    color_bad = RGB(255, 107, 107);
    color_ok = RGB(78, 209, 139);
    color_warn = RGB(232, 180, 74);
    accent = RGB(40, 199, 111);
    accent_foreground = RGB(14, 16, 19);

    tray_color_background = RGB(32, 32, 32);
    tray_color_border = RGB(58, 58, 58);
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
    color_text = RGB(30, 35, 42);
    color_text_dim = RGB(94, 103, 116);
    color_text_faint = RGB(138, 147, 160);
    color_bad = RGB(201, 60, 60);
    color_ok = RGB(21, 159, 99);
    color_warn = RGB(183, 121, 31);
    accent = RGB(21, 159, 99);
    accent_foreground = RGB(255, 255, 255);

    tray_color_background = RGB(255, 255, 255);
    tray_color_border = RGB(217, 222, 230);
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

void initialize(const ConfigStore& config) {
    mode = parse_mode(config.string(L"appearance.theme", L"system"));
    apply_mode(mode);
}

void apply_mode(Mode value) noexcept {
    mode = value;
    dark = value == Mode::dark ||
           (value == Mode::system && system_uses_dark_mode());
    if (dark) {
        apply_dark_palette();
    } else {
        apply_light_palette();
    }
    update_aliases();
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
}

void apply_control_theme(HWND control) noexcept {
    if (control) SetWindowTheme(control, dark ? L"DarkMode_Explorer" : L"Explorer", nullptr);
}

}  // namespace screentrans::theme
