#pragma once

#include <windows.h>
#include <d2d1.h>

#include <string_view>

namespace minitrans::ui_draw {

// One process-wide single-threaded D2D factory. Direct2D requires only that
// calls sharing a factory stay on one thread, and every caller here draws on
// the main UI thread, so a shared instance is safe and saves each module from
// creating its own.
inline ID2D1Factory* d2d_factory() noexcept {
    static ID2D1Factory* factory = [] {
        ID2D1Factory* value = nullptr;
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &value);
        return value;
    }();
    return factory;
}

// Fills `rect` with `fill` and outlines it with `border` at `radius`, drawn
// through Direct2D so the corners stay smooth at any DPI. Falls back to a GDI
// RoundRect only when D2D cannot create a render target, which keeps a window
// paintable when D2D initialization fails.
inline void rounded_rect(HDC dc, const RECT& rect, int radius, COLORREF fill,
                         COLORREF border) {
    ID2D1Factory* const factory = d2d_factory();
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
