#pragma once

#include "config.hpp"

#include <windows.h>

#include <string>
#include <string_view>

namespace screentrans::theme {

inline constexpr UINT changed_message = WM_APP + 90;

enum class Mode {
    system,
    light,
    dark,
};

// Shared, process-wide palette. Windows repaint from these values after a
// theme change; keeping one palette prevents individual windows from drifting.
inline Mode mode = Mode::system;
inline bool dark = true;

// Typography is shared by all windows so spacing and hierarchy remain
// consistent with the platform's system UI font.
inline std::wstring font_family = L"Segoe UI";
constexpr int font_size_body = 13;
constexpr int font_size_title = 18;
constexpr int font_size_section = 14;
constexpr int font_size_small = 11;
constexpr int font_size_translation = 14;

inline COLORREF color_page = RGB(21, 22, 27);
inline COLORREF color_sidebar = RGB(16, 17, 22);
inline COLORREF color_card = RGB(27, 29, 35);
inline COLORREF color_input = RGB(33, 36, 41);
inline COLORREF color_translation = RGB(25, 27, 32);
inline COLORREF color_line = RGB(40, 43, 51);
inline COLORREF color_line_high = RGB(52, 56, 66);
inline COLORREF color_hover = RGB(45, 48, 56);
inline COLORREF color_nav_hover = RGB(34, 36, 42);
inline COLORREF color_nav_active = RGB(39, 42, 49);
inline COLORREF color_text = RGB(231, 233, 236);
inline COLORREF color_text_dim = RGB(148, 154, 164);
inline COLORREF color_text_faint = RGB(110, 116, 126);
inline COLORREF color_bad = RGB(255, 107, 107);
inline COLORREF color_ok = RGB(78, 209, 139);
inline COLORREF color_warn = RGB(232, 180, 74);
inline COLORREF accent = RGB(40, 199, 111);
inline COLORREF accent_foreground = RGB(14, 16, 19);

inline COLORREF color_background = color_page;
inline COLORREF color_surface = color_card;
inline COLORREF color_border = color_line;
inline COLORREF color_dim = color_text_dim;
inline COLORREF color_faint = color_text_faint;
inline COLORREF color_error = color_bad;
inline COLORREF color_warning = color_warn;

inline COLORREF tray_color_background = RGB(32, 32, 32);
inline COLORREF tray_color_border = RGB(58, 58, 58);
inline COLORREF tray_color_separator = RGB(61, 61, 61);
inline COLORREF tray_color_hover = RGB(48, 48, 48);
inline COLORREF tray_color_text = RGB(243, 243, 243);
inline COLORREF tray_color_text_dim = RGB(178, 178, 178);
inline COLORREF tray_color_text_faint = RGB(112, 112, 112);

[[nodiscard]] bool system_uses_dark_mode() noexcept;
[[nodiscard]] std::wstring mode_name(Mode value);
[[nodiscard]] Mode parse_mode(std::wstring_view value) noexcept;

void initialize(const ConfigStore& config);
void apply_mode(Mode value) noexcept;
void refresh_system_mode() noexcept;
void refresh_system_ui_font();

void apply_window_theme(HWND window, bool round_corners = true,
                        bool custom_caption = false) noexcept;
void apply_control_theme(HWND control) noexcept;

}  // namespace screentrans::theme
