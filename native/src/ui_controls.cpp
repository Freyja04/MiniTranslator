#include "ui_controls.hpp"

#include <algorithm>

namespace minitrans::ui_controls {
namespace {

void fill_rect(HDC dc, const RECT& bounds, COLORREF color) {
    if (!dc) return;
    HBRUSH brush = CreateSolidBrush(color);
    FillRect(dc, &bounds, brush);
    DeleteObject(brush);
}

void draw_text_line(HDC dc, std::wstring_view text, RECT bounds, HFONT font,
                    COLORREF color, UINT format) {
    if (!dc || !font || text.empty()) return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    const HGDIOBJ previous = SelectObject(dc, font);
    DrawTextW(dc, text.data(), static_cast<int>(text.size()), &bounds,
              format | DT_NOPREFIX);
    SelectObject(dc, previous);
}

}  // namespace

int scaled(int logical, int dpi) noexcept {
    return MulDiv(logical, dpi > 0 ? dpi : 96, 96);
}

COLORREF mix_color(COLORREF base, COLORREF tint, int tint_percent) noexcept {
    const int amount = std::clamp(tint_percent, 0, 100);
    const auto channel = [amount](BYTE base_value, BYTE tint_value) {
        return static_cast<BYTE>(
            (base_value * (100 - amount) + tint_value * amount) / 100);
    };
    return RGB(channel(GetRValue(base), GetRValue(tint)),
               channel(GetGValue(base), GetGValue(tint)),
               channel(GetBValue(base), GetBValue(tint)));
}

void draw_field(HDC dc, const RECT& bounds, std::wstring_view text,
                const FieldStyle& style, RoundRectPainter painter) {
    if (!dc || !painter) return;
    fill_rect(dc, bounds, style.background);
    painter(dc, bounds, scaled(metrics::field_radius, style.dpi), style.fill,
            style.focused ? style.focus_border : style.border);

    RECT text_bounds = bounds;
    text_bounds.left += scaled(metrics::field_text_left, style.dpi);
    text_bounds.right -= scaled(metrics::field_text_right, style.dpi);
    draw_text_line(dc, text, text_bounds, style.font,
                   style.enabled ? style.text : style.disabled_text,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    const int center_x =
        bounds.right - scaled(metrics::chevron_center_from_right, style.dpi);
    const int center_y = (bounds.top + bounds.bottom) / 2;
    const int half_width = scaled(metrics::chevron_half_width, style.dpi);
    const int half_height = scaled(metrics::chevron_half_height, style.dpi);
    HPEN pen = CreatePen(PS_SOLID, std::max(1, scaled(metrics::chevron_stroke,
                                                       style.dpi)),
                         style.chevron);
    const HGDIOBJ previous_pen = SelectObject(dc, pen);
    MoveToEx(dc, center_x - half_width, center_y - half_height, nullptr);
    LineTo(dc, center_x, center_y + half_height);
    LineTo(dc, center_x + half_width, center_y - half_height);
    SelectObject(dc, previous_pen);
    DeleteObject(pen);
}

void draw_field_item(HDC dc, const RECT& bounds, std::wstring_view text,
                     const FieldItemStyle& style, RoundRectPainter painter) {
    if (!dc || !painter) return;
    fill_rect(dc, bounds, style.background);

    if (style.selected) {
        RECT highlight = bounds;
        InflateRect(&highlight, -scaled(metrics::item_inset_x, style.dpi),
                    -scaled(metrics::item_inset_y, style.dpi));
        const COLORREF fill = mix_color(
            style.background, style.accent, metrics::item_selection_fill_percent);
        const COLORREF border = mix_color(style.background, style.accent,
                                          metrics::item_selection_border_percent);
        painter(dc, highlight, scaled(metrics::item_radius, style.dpi), fill,
                border);

        RECT marker = highlight;
        marker.left += scaled(metrics::item_marker_gap, style.dpi);
        marker.right = marker.left + std::max(
            1, scaled(metrics::item_marker_width, style.dpi));
        marker.top += scaled(metrics::item_marker_inset_y, style.dpi);
        marker.bottom -= scaled(metrics::item_marker_inset_y, style.dpi);
        fill_rect(dc, marker, style.accent);
    }

    RECT text_bounds = bounds;
    text_bounds.left += scaled(metrics::item_text_left, style.dpi);
    text_bounds.right -= scaled(metrics::item_text_right, style.dpi);
    draw_text_line(dc, text, text_bounds, style.font,
                   style.disabled ? style.disabled_text : style.text,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
}

void draw_checkbox(HDC dc, const RECT& bounds, std::wstring_view caption,
                   const CheckboxStyle& style, RoundRectPainter painter) {
    if (!dc || !painter) return;
    fill_rect(dc, bounds, style.background);

    const int size = std::max(1, scaled(metrics::checkbox_size, style.dpi));
    const int gap = scaled(metrics::checkbox_gap, style.dpi);
    const int text_gap = scaled(6, style.dpi);

    int caption_width = 0;
    if (style.font) {
        const HGDIOBJ previous = SelectObject(dc, style.font);
        SIZE measured{};
        if (!caption.empty() &&
            GetTextExtentPoint32W(dc, caption.data(),
                                  static_cast<int>(caption.size()),
                                  &measured)) {
            caption_width = measured.cx;
        }
        SelectObject(dc, previous);
    }

    const int box_left = std::min(
        bounds.right - size, bounds.left + caption_width + gap);
    const int box_top = bounds.top + (bounds.bottom - bounds.top - size) / 2;
    RECT box{box_left, box_top, box_left + size, box_top + size};
    painter(dc, box, scaled(metrics::checkbox_radius, style.dpi),
            style.checked ? style.checked_fill : style.fill,
            style.checked ? style.checked_fill : style.border);

    const LONG text_right = std::max<LONG>(bounds.left, box_left - text_gap);
    RECT text_bounds{bounds.left, bounds.top, text_right, bounds.bottom};
    draw_text_line(dc, caption, text_bounds, style.font,
                   style.enabled ? style.text : style.disabled_text,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (style.checked) {
        const auto point = [size, &box](int x, int y) {
            return POINT{box.left + x * size / 16, box.top + y * size / 16};
        };
        HPEN pen = CreatePen(PS_SOLID,
                             std::max(1, scaled(metrics::checkbox_check_stroke,
                                                style.dpi)),
                             style.check);
        const HGDIOBJ previous_pen = SelectObject(dc, pen);
        const POINT first = point(4, 8);
        const POINT second = point(7, 11);
        const POINT third = point(13, 5);
        MoveToEx(dc, first.x, first.y, nullptr);
        LineTo(dc, second.x, second.y);
        LineTo(dc, third.x, third.y);
        SelectObject(dc, previous_pen);
        DeleteObject(pen);
    }
}

void draw_icon_button(HDC dc, const RECT& bounds, wchar_t glyph, HFONT font,
                      int dpi, COLORREF fill, COLORREF border, COLORREF color,
                      RoundRectPainter painter) {
    if (!dc || !painter) return;
    painter(dc, bounds, scaled(metrics::icon_button_radius, dpi), fill, border);
    if (!font) return;
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, color);
    const HGDIOBJ previous = SelectObject(dc, font);
    RECT text_bounds = bounds;
    DrawTextW(dc, &glyph, 1, &text_bounds,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, previous);
}

}  // namespace minitrans::ui_controls
