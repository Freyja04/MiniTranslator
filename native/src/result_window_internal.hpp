#pragma once

#include <d2d1.h>
#include <windows.h>

#include <algorithm>

namespace minitrans::result_window_internal {

// Shared by the result window's lifecycle, render, and control-bar units.
inline constexpr COLORREF resize_transparent_key = RGB(1, 2, 3);

inline int dpi_value(HWND window, int logical) {
    const UINT dpi = window ? GetDpiForWindow(window) : 96;
    return std::max(1, MulDiv(logical, static_cast<int>(dpi ? dpi : 96), 96));
}

inline bool equal_rect(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top
        && left.right == right.right && left.bottom == right.bottom;
}

inline bool equal_size(const RECT& left, const RECT& right) {
    return left.right - left.left == right.right - right.left
        && left.bottom - left.top == right.bottom - right.top;
}

ID2D1Factory* d2d_factory() noexcept;
ID2D1StrokeStyle* round_stroke_style() noexcept;
D2D1_COLOR_F d2d_color(COLORREF color, float alpha = 1.0F) noexcept;
void draw_control_icon(ID2D1RenderTarget* target, int index,
                       const D2D1_RECT_F& bounds, COLORREF color,
                       float opacity = 1.0F);

}  // namespace minitrans::result_window_internal
