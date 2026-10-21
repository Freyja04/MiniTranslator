#pragma once

#include <windows.h>

#include <string_view>

namespace minitrans::ui_draw {

// Draws single-shot UI text without leaking the caller's font or text color.
inline void draw_text(HDC dc, std::wstring_view text, RECT rect, HFONT font,
                      COLORREF color, UINT format) {
    if (!dc || !font) return;
    const HGDIOBJ previous = SelectObject(dc, font);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &rect,
              format | DT_NOPREFIX);
    SelectObject(dc, previous);
}

inline std::wstring text_of(HWND control) {
    const int length = control ? GetWindowTextLengthW(control) : 0;
    if (length <= 0) return {};
    std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
    GetWindowTextW(control, value.data(), length + 1);
    value.resize(static_cast<std::size_t>(length));
    return value;
}

inline void set_font(HWND control, HFONT font) {
    if (control) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font), TRUE);
    }
}

}  // namespace minitrans::ui_draw
