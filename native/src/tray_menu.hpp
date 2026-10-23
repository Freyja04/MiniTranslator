#pragma once

#include <windows.h>

#include <cstdint>
#include <string>

namespace minitrans::tray_menu {

enum class Icon {
    none,
    check,
    capture,
    text,
    brand,
    restore,
    home,
    exit,
};

struct Item {
    std::uint32_t magic{0x53544D49};  // STMI
    UINT command{};
    Icon icon{Icon::none};
    std::wstring label;
    std::wstring secondary;
    std::wstring accessible_text;
    UINT dpi{96};
    HFONT font{};
    bool enabled{true};
    bool separator{};
    bool first{};
    bool last{};
};

int scaled(int logical, UINT dpi) noexcept;
UINT menu_dpi(POINT location) noexcept;
HFONT create_font(UINT dpi) noexcept;
void CALLBACK style_menu_window(HWINEVENTHOOK hook, DWORD event, HWND window,
                                LONG object, LONG child, DWORD thread,
                                DWORD time) noexcept;
bool is_menu_item(ULONG_PTR data) noexcept;
void draw_icon(HDC dc, const RECT& bounds, Icon icon, COLORREF color, UINT dpi);
bool measure_item(MEASUREITEMSTRUCT& measure);
bool draw_item(const DRAWITEMSTRUCT& draw);
void layout_self_test();
HICON load_app_icon(HINSTANCE instance) noexcept;
POINT anchor_for_quick_translation(HWND owner, UINT icon_id) noexcept;

}  // namespace minitrans::tray_menu
