#pragma once

#include <windows.h>

#include <string_view>

namespace minitrans::ui_controls {

// Logical metrics shared by every self-drawn control. The settings window and
// the quick translation panel used to carry their own copies of these numbers,
// which is how the two combo boxes drifted apart; keeping them here makes the
// drawn result identical everywhere.
namespace metrics {

constexpr int field_radius = 6;
constexpr int field_text_left = 9;
constexpr int field_text_right = 32;
constexpr int chevron_half_width = 4;
constexpr int chevron_half_height = 2;
constexpr int chevron_center_from_right = 16;
constexpr int chevron_stroke = 2;

constexpr int item_radius = 5;
constexpr int item_inset_x = 4;
constexpr int item_inset_y = 2;
constexpr int item_text_left = 13;
constexpr int item_text_right = 9;
constexpr int item_marker_gap = 3;
constexpr int item_marker_width = 2;
constexpr int item_marker_inset_y = 6;
constexpr int item_selection_fill_percent = 16;
constexpr int item_selection_border_percent = 34;

constexpr int checkbox_size = 16;
constexpr int checkbox_radius = 4;
constexpr int checkbox_gap = 9;
constexpr int checkbox_check_stroke = 2;

constexpr int icon_button_radius = 7;

}  // namespace metrics

// Rounded rectangles are painted by the owning window: the settings window uses
// the antialiased D2D surface while the quick panel draws with GDI. The shared
// control code only owns layout and colours, so both keep their corner quality.
using RoundRectPainter = void (*)(HDC dc, const RECT& bounds, int radius,
                                  COLORREF fill, COLORREF border);

struct FieldStyle {
    COLORREF background{};    // painted behind the rounded field
    COLORREF fill{};
    COLORREF border{};
    COLORREF focus_border{};
    COLORREF text{};
    COLORREF disabled_text{};
    COLORREF chevron{};
    HFONT font{};
    int dpi{96};
    bool enabled{true};
    bool focused{false};
};

struct FieldItemStyle {
    COLORREF background{};
    COLORREF text{};
    COLORREF disabled_text{};
    COLORREF accent{};
    HFONT font{};
    int dpi{96};
    bool selected{false};
    bool disabled{false};
};

struct CheckboxStyle {
    COLORREF background{};
    COLORREF fill{};
    COLORREF border{};
    COLORREF checked_fill{};
    COLORREF check{};
    COLORREF text{};
    COLORREF disabled_text{};
    HFONT font{};
    int dpi{96};
    bool checked{false};
    bool enabled{true};
};

[[nodiscard]] int scaled(int logical, int dpi) noexcept;

[[nodiscard]] COLORREF mix_color(COLORREF base, COLORREF tint,
                                 int tint_percent) noexcept;

// Closed combo box: rounded field, one line of text and the chevron.
void draw_field(HDC dc, const RECT& bounds, std::wstring_view text,
                const FieldStyle& style, RoundRectPainter painter);

// One row of an open combo box list.
void draw_field_item(HDC dc, const RECT& bounds, std::wstring_view text,
                     const FieldItemStyle& style, RoundRectPainter painter);

// Checkbox option row: caption first, the box sits right behind the text.
void draw_checkbox(HDC dc, const RECT& bounds, std::wstring_view caption,
                   const CheckboxStyle& style, RoundRectPainter painter);

// Rounded icon button used by the header toolbars (capture / close / swap).
void draw_icon_button(HDC dc, const RECT& bounds, wchar_t glyph, HFONT font,
                      int dpi, COLORREF fill, COLORREF border,
                      COLORREF color, RoundRectPainter painter);

}  // namespace minitrans::ui_controls
