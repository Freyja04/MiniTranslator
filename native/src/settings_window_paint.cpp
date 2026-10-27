#include "settings_window.hpp"
#include "settings_window_internal.hpp"
#include "ui_controls.hpp"

namespace minitrans {

using namespace settings_internal;


void SettingsWindow::draw_ai_row(HDC dc, const RECT& item,
                                 const settings_ai_list::Row& row, bool hovered,
                                 settings_ai_list::Button hover_button,
                                 bool confirming, bool last) {
    // The row sits on the page card, so it erases with the card colour and
    // keeps the hairline separators the other pages use.
    FillRect(dc, &item, card_background_);
    const int padding = px(settings_ai_list::logical_row_padding(), dpi_);
    if (!last) {
        // The separator spans the full row width, the same line the layout
        // draws between the page's other rows - padded variants read as
        // different lengths next to it.
        HPEN pen = CreatePen(PS_SOLID, 1, color_line);
        const auto previous = SelectObject(dc, pen);
        const int y = item.bottom - 1;
        MoveToEx(dc, item.left, y, nullptr);
        LineTo(dc, item.right, y);
        SelectObject(dc, previous);
        DeleteObject(pen);
    }
    // The buttons and the narrow flag come from the row rectangle itself, which
    // is the same rectangle the click was hit-tested against; the row kind
    // decides whether the row carries only the toggle, toggle plus 编辑, or
    // the full endpoint set.
    bool narrow = false;
    const auto actions = settings_ai_list::buttons(item, dpi_, &row, &narrow);
    static_cast<void>(narrow);
    // The row content keeps the card's inner padding; the name starts at the
    // same edge as every other control on the page.
    // The name starts exactly at the separator's left end, so the text and
    // the line under it share one alignment edge.
    const int text_left = item.left;
    const int name_right = actions.empty()
        ? item.right - padding
        : std::max(text_left, static_cast<int>(actions.front().second.left) -
                                  px(logical_provider_gap, dpi_));
    draw_text(dc, row.name,
              RECT{text_left, item.top, std::max(text_left, name_right), item.bottom},
              font_,
              // A row that is not ready yet reads as a caption, and the row
              // under the cursor always brightens so the pointer is traceable.
              !row.configured && !hovered ? color_text_dim
                            : hovered && hover_button == settings_ai_list::Button::none
                                  ? color_text
                                  : row.configured ? color_text : color_text_dim,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    for (const auto& [button, bounds] : actions) {
        // An endpoint without an address or model cannot be switched on, so the
        // enable control reads as off and inert instead of failing on click.
        const bool usable = button != settings_ai_list::Button::enable ||
                            row.configured;
        const bool active = usable && hovered && hover_button == button;
        if (button == settings_ai_list::Button::enable) {
            // The toggle is drawn like the Windows settings switch: a pill
            // whose knob slides to the themed edge, filling with the text
            // colour when on. Everything runs through the antialiased rounded
            // painter - including the knob, a square with a full-radius
            // corner - so the circle never picks up GDI jaggies.
            FillRect(dc, &bounds, card_background_);
            const int pill_height = px(14, dpi_);
            const int pill_w = px(26, dpi_);
            const int pill_top =
                bounds.top + (bounds.bottom - bounds.top - pill_height) / 2;
            const int pill_left = bounds.left +
                (bounds.right - bounds.left - pill_w) / 2;
            RECT pill{pill_left, pill_top, pill_left + pill_w,
                      pill_top + pill_height};
            const COLORREF on = theme::color_text;
            // The off pill keeps enough contrast against the card that both
            // states read at the same size; a darker fill made the off
            // switch look like a lone floating knob.
            const COLORREF off = usable ? (active ? color_line_high : color_line_high)
                                        : color_line;
            fill_round_rect(dc, pill, pill_height / 2,
                            row.enabled ? on : off,
                            row.enabled ? on : off);
            const int knob = pill_height - px(4, dpi_);
            const int inset = px(2, dpi_);
            RECT knob_rect{row.enabled ? pill.right - knob - inset
                                       : pill.left + inset,
                           pill.top + inset,
                           row.enabled ? pill.right - inset
                                       : pill.left + inset + knob,
                           pill.bottom - inset};
            fill_round_rect(dc, knob_rect, knob / 2,
                            row.enabled ? theme::color_page
                                        : usable ? color_page : color_text_faint,
                            row.enabled ? theme::color_page : color_page);
            continue;
        }
        // 编辑 and 删除 are bare glyphs: hover brightens the icon itself, no
        // chip background around it.
        const COLORREF text = button == settings_ai_list::Button::remove
                                  ? (confirming ? color_bad
                                   : active ? color_text : color_text_dim)
                                  : active ? color_text : color_text_dim;
        const HFONT glyph_font = action_icon_font_ ? action_icon_font_ : icon_font_;
        if (glyph_font) {
            const wchar_t glyph = button == settings_ai_list::Button::edit
                ? static_cast<wchar_t>(0xE70F)
                : confirming ? static_cast<wchar_t>(0xE74A)
                             : ui_icons::delete_glyph;
            draw_text(dc, std::wstring_view(&glyph, 1), bounds, glyph_font, text,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            const wchar_t* caption = button == settings_ai_list::Button::edit
                ? L"编" : confirming ? L"确" : L"删";
            draw_text(dc, caption, bounds, small_font_, text,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
    }
}

void SettingsWindow::paint() {
    PAINTSTRUCT state{};
    HDC target = BeginPaint(window_, &state);
    if (!target) {
        EndPaint(window_, &state);
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    if (client.right <= client.left || client.bottom <= client.top) {
        EndPaint(window_, &state);
        return;
    }
    HDC dc = CreateCompatibleDC(target);
    if (!dc) {
        FillRect(target, &client, background_);
        EndPaint(window_, &state);
        return;
    }
    HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
    if (!bitmap) {
        FillRect(target, &client, background_);
        DeleteDC(dc);
        EndPaint(window_, &state);
        return;
    }
    const auto old_bitmap = SelectObject(dc, bitmap);
    FillRect(dc, &client, background_);

    const int sidebar = px(logical_sidebar_width, dpi_);
    RECT side{0, 0, sidebar, client.bottom};
    FillRect(dc, &side, sidebar_background_);
    HPEN divider = CreatePen(PS_SOLID, 1, color_line);
    const auto old_pen = SelectObject(dc, divider);
    MoveToEx(dc, sidebar - 1, 0, nullptr);
    LineTo(dc, sidebar - 1, client.bottom);
    SelectObject(dc, old_pen);
    DeleteObject(divider);

    const int card_left = sidebar + px(logical_page_margin, dpi_);
    const int card_right = client.right - px(logical_page_margin, dpi_);
    const int inner_left = card_left + px(18, dpi_);
    const int inner_right = card_right - px(18, dpi_);
    const int field_offset = field_offset_ > 0
        ? field_offset_
        : px(logical_field_min_offset, dpi_);
    const int field_left = inner_left + field_offset;
    const int text_right = field_left - px(logical_label_gap, dpi_);

    const auto card = [&](int top, int bottom) {
        fill_round_rect(dc, RECT{card_left, px(top, dpi_), card_right, px(bottom, dpi_)},
                        px(7, dpi_), color_card, color_line);
    };
    const auto separator_line = [&](int left, int right, int y) {
        HPEN pen = CreatePen(PS_SOLID, 1, color_line);
        const auto previous = SelectObject(dc, pen);
        MoveToEx(dc, left, y, nullptr);
        LineTo(dc, right, y);
        SelectObject(dc, previous);
        DeleteObject(pen);
    };
    const auto separator = [&](int logical_y) {
        separator_line(inner_left, inner_right, px(logical_y, dpi_));
    };
    const auto group = [&](int top, int rows) {
        const int bottom = top + rows * logical_setting_row_height;
        card(top, bottom);
        for (int row = 1; row < rows; ++row) {
            separator(top + row * logical_setting_row_height);
        }
        return bottom;
    };
    const auto section = [&](std::wstring_view text, int top) {
        draw_text(dc, text,
                  RECT{card_left + px(2, dpi_), px(top, dpi_),
                       card_right, px(top + 24, dpi_)},
                  small_font_, color_text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    };
    const auto row_copy = [&](std::wstring_view title, std::wstring_view description,
                              int top, bool paint_title) {
        if (paint_title && !title.empty()) {
            draw_text(dc, title,
                      RECT{inner_left, px(top + 7, dpi_), text_right,
                           px(top + 29, dpi_)},
                      font_, color_text, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                                               DT_END_ELLIPSIS);
        }
        if (!description.empty()) {
            draw_text(dc, description,
                      RECT{inner_left, px(top + 30, dpi_), text_right,
                           px(top + 52, dpi_)},
                      small_font_, color_text_dim, DT_LEFT | DT_VCENTER |
                                                   DT_SINGLELINE | DT_END_ELLIPSIS);
        }
    };
    if (current_page_ == 0) {
        const int first_top = logical_content_top + logical_card_padding;
        const int block_height = logical_control_height;
        const int stride = block_height + logical_row_gap;
        const int main_bottom = first_top + stride * 3 - logical_row_gap +
                                logical_card_padding;
        card(logical_content_top, main_bottom);
    } else if (current_page_ == 1) {
        // The layout pass measured this page against the controls it placed, so
        // the card and its separators are painted from that geometry instead of
        // repeating the row arithmetic here.
        const auto& geometry = translation_;
        fill_round_rect(dc,
                        RECT{card_left, geometry.card_top, card_right,
                             geometry.card_bottom},
                        px(7, dpi_), color_card, color_line);
        for (int index = 0; index < geometry.separator_count; ++index) {
            separator_line(inner_left, inner_right, geometry.separators[index]);
        }
    } else if (current_page_ == 2) {
        card(logical_content_top, logical_ocr_main_bottom);
        section(L"安装其他语言", logical_ocr_language_section_top);
        card(logical_ocr_language_card_top,
             logical_ocr_language_card_bottom);
        draw_text(dc,
                  L"需要其他语言时，到 Windows 设置添加该语言，再进入语言选项安装光学字符识别，装好后重启本程序即可。",
                  RECT{inner_left, px(logical_ocr_language_note_top, dpi_),
                       inner_right,
                       px(logical_ocr_language_note_top +
                              logical_ocr_language_note_height,
                          dpi_)},
                  small_font_, color_text_dim, DT_LEFT | DT_TOP | DT_WORDBREAK);
    } else if (current_page_ == 3) {
        // Theme switch, startup option and maintenance buttons share one card.
        // The startup/restart hints were dropped, so the card ends right after
        // the button row.
        const int first_top = logical_content_top + logical_card_padding;
        const int autostart_top = first_top + logical_setting_row_height;
        const int separator_top =
            autostart_top + logical_control_height + logical_row_gap;
        const int actions_top = separator_top + logical_row_gap;
        const int main_bottom =
            actions_top + logical_control_height + logical_card_padding;
        card(logical_content_top, main_bottom);
        separator(separator_top);
    } else if (current_page_ == 4) {
        // Matches the quick translation window: one card split by a divider,
        // with the captions and their icons inside the card.
        const int inset = px(logical_translation_inset, dpi_);
        const int body_top = px(logical_translation_body_top, dpi_);
        const int body_bottom = px(logical_translation_body_bottom, dpi_);
        const int divider_y = px(logical_translation_divider, dpi_);
        const int label_height = px(logical_translation_label_height, dpi_);
        const RECT body{card_left, body_top, card_right, body_bottom};
        // The quick window draws its card with GDI RoundRect and a 10px corner
        // ellipse, which reads as a 5px radius; match that here.
        fill_round_rect(dc, body, px(5, dpi_), color_card, color_line);

        HPEN divider_pen = CreatePen(PS_SOLID, 1, color_line);
        const HGDIOBJ previous_pen = SelectObject(dc, divider_pen);
        MoveToEx(dc, body.left + inset, divider_y, nullptr);
        LineTo(dc, body.right - inset, divider_y);
        SelectObject(dc, previous_pen);
        DeleteObject(divider_pen);

        const int label_left = body.left + inset;
        const int label_right = label_left + px(80, dpi_);
        draw_text(dc, L"原文",
                  RECT{label_left, body_top, label_right, body_top + label_height},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, L"译文",
                  RECT{label_left, divider_y, label_right, divider_y + label_height},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const int footer_top = px(logical_translation_footer_top, dpi_);
        const int footer_height = px(logical_hint_height, dpi_);
        const int footer_status_right = card_right - px(220, dpi_);
        if (text_session_) {
            const bool copied = text_session_->copy_confirmation_visible();
            const bool over_limit =
                text_session_->state() == TextTranslationState::too_long;
            const bool error = !copied &&
                               (text_session_->state() == TextTranslationState::error ||
                                over_limit);
            draw_text(dc, copied ? L"已复制到剪贴板"
                                 : over_limit
                                       ? L"已超出 500 字上限"
                                       : text_translation_status(*text_session_),
                      RECT{card_left, footer_top, footer_status_right,
                           footer_top + footer_height},
                      small_font_,
                      error ? color_bad : color_text_dim,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        draw_text(dc, L"Ctrl+Enter 翻译 · 停止输入后自动翻译",
                  RECT{card_left, footer_top, card_right,
                       footer_top + footer_height},
                  small_font_, color_text_faint,
                  DT_RIGHT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    for (const auto control : page_controls_[static_cast<std::size_t>(current_page_)]) {
        if (!control || !IsWindowVisible(control)) continue;
        wchar_t type[24]{};
        GetClassNameW(control, type, static_cast<int>(std::size(type)));
        if (_wcsicmp(type, L"Edit") != 0 || control == text_input_ ||
            control == text_output_) {
            continue;
        }
        RECT frame{};
        GetWindowRect(control, &frame);
        MapWindowPoints(HWND_DESKTOP, window_, reinterpret_cast<POINT*>(&frame), 2);
        const int inset_x = std::max(1, px(logical_edit_inset_x, dpi_));
        const int inset_top = std::max(1, px(logical_edit_inset_top, dpi_));
        const int inset_bottom = std::max(1, px(logical_edit_inset_bottom, dpi_));
        frame.left -= inset_x;
        frame.right += inset_x;
        frame.top -= inset_top;
        frame.bottom += inset_bottom;
        fill_round_rect(dc, frame, px(logical_edit_corner_radius, dpi_), color_input,
                        color_line);
    }

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window_, &state);
}

LRESULT SettingsWindow::draw_item(const DRAWITEMSTRUCT& item) {
    HDC dc = item.hDC;
    RECT rect = item.rcItem;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hot = (item.itemState & ODS_HOTLIGHT) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const COLORREF accent = theme::accent;

    if (item.CtlType == ODT_LISTBOX && item.hwndItem == ai_list_) {
        // The rows live in the list box so a long list scrolls with the
        // separators and buttons attached to the right row. Both branches
        // share the control; shown_rows() carries whichever is on screen.
        const auto& rows = shown_rows();
        const auto index = static_cast<std::size_t>(item.itemID);
        const bool last = index + 1 >= rows.size();
        const bool confirming = large_model() && index < rows.size() &&
                                rows[index].name == pending_delete_;
        draw_ai_row(dc, rect, index < rows.size() ? rows[index]
                                                  : settings_ai_list::Row{},
                    static_cast<int>(item.itemID) == ai_list_hover_,
                    ai_hover_button_, confirming, last);
        return TRUE;
    }

    if (item.CtlType == ODT_BUTTON && item.CtlID == id_text_swap) {
        // The language row floats on the page background, so the swap glyph has
        // to erase with the page colour rather than the card surface.
        FillRect(dc, &rect, background_);
        if (icon_font_) {
            draw_text(dc, std::wstring_view(L"\uE8AB", 1), rect, icon_font_,
                      hot ? color_text : color_text_dim,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        return TRUE;
    }

    if (item.CtlType == ODT_BUTTON && item.CtlID == id_text_capture) {
        // Mirrors the quick translation window: a rounded square that sits on
        // the page background at the left edge of the language row.
        FillRect(dc, &rect, background_);
        const COLORREF capture_fill = pressed ? theme::color_hover
                                     : hot ? theme::color_line_high : color_card;
        ui_controls::draw_icon_button(
            dc, rect, ui_icons::capture_translation, icon_font_, dpi_,
            capture_fill, hot ? accent : color_line,
            disabled ? color_text_faint
                     : hot ? color_text : color_text_dim,
            &settings_internal::fill_round_rect);
        return TRUE;
    }

    if (item.CtlType == ODT_COMBOBOX) {
        ui_controls::draw_combo_item(item, font_, dpi_, color_input,
                                     &settings_internal::fill_round_rect);
        return TRUE;
    }

    if (item.CtlType != ODT_BUTTON) return FALSE;
    if (item.CtlID >= id_nav_first && item.CtlID <= id_nav_last) {
        const int navigation_index = static_cast<int>(item.CtlID) - id_nav_first;
        const int page = navigation_page_order[static_cast<std::size_t>(navigation_index)];
        const bool active = page == current_page_;
        const COLORREF fill = pressed ? theme::color_hover
                              : active ? color_nav_active
                              : hot ? color_nav_hover : color_sidebar;
        FillRect(dc, &rect, sidebar_background_);
        fill_round_rect(dc, rect, px(6, dpi_), fill, fill);
        const COLORREF icon_color = active ? color_text : color_text_dim;
        const COLORREF text_color = active ? color_text : color_text_dim;
        const int icon_size = px(18, dpi_);
        const int icon_top = rect.top + (rect.bottom - rect.top - icon_size) / 2;
        RECT icon_rect{rect.left + px(13, dpi_), icon_top,
                       rect.left + px(13, dpi_) + icon_size, icon_top + icon_size};
        draw_settings_nav_icon(dc, page, icon_rect, icon_color, icon_font_);
        RECT text_rect{rect.left + px(43, dpi_), rect.top, rect.right - px(8, dpi_), rect.bottom};
        draw_text(dc, page_names[static_cast<std::size_t>(page)], text_rect, font_, text_color,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }

    if (item.hwndItem == ai_add_) {
        // 添加 reads as a circle with a plus, the quieter sibling of the
        // other icon buttons on this page.
        FillRect(dc, &rect, card_background_);
        const int diameter =
            std::min({static_cast<int>(rect.right - rect.left),
                      static_cast<int>(rect.bottom - rect.top), px(22, dpi_)});
        // The circle sits at the rect's right edge - the row toggles below
        // close at the same column, so the two read as one right-aligned
        // column of controls.
        const int left = rect.right - diameter - px(4, dpi_);
        const int top = rect.top + (rect.bottom - rect.top - diameter) / 2;
        RECT circle{left, top, left + diameter, top + diameter};
        const COLORREF stroke = disabled ? color_line
                                : (pressed || hot) ? color_text : color_text_dim;
        // The ring runs through the antialiased rounded painter (a full-radius
        // rounded square is the circle), filled with the card colour so only
        // the outline reads.
        fill_round_rect(dc, circle, diameter / 2, theme::color_card, stroke);
        const HFONT glyph_font = action_icon_font_ ? action_icon_font_ : icon_font_;
        if (glyph_font) {
            const int inset = diameter / 5;
            RECT glyph_rect{circle.left + inset, circle.top + inset,
                            circle.right - inset, circle.bottom - inset};
            draw_text(dc, std::wstring_view(&ui_icons::add_glyph, 1), glyph_rect,
                      glyph_font,
                      disabled ? color_text_faint : color_text_dim,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        } else {
            draw_text(dc, L"+", rect, small_font_,
                      disabled ? color_text_faint : color_text_dim,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        }
        return TRUE;
    }

    if (item.hwndItem == class_traditional_ || item.hwndItem == class_ai_ ||
        item.hwndItem == theme_mode_ || item.hwndItem == theme_segment_[0] ||
        item.hwndItem == theme_segment_[1]) {
        // One segmented row: the active chip reads as a raised field and the
        // inactive ones as plain captions, which is the look of the protocol
        // row inside the endpoint dialog. The two class halves keep the old
        // accent fill because that pair is the page's primary switch; the
        // theme chips follow the quieter segmented style.
        const bool theme_chip = item.hwndItem == theme_mode_ ||
                                item.hwndItem == theme_segment_[0] ||
                                item.hwndItem == theme_segment_[1];
        const bool active = theme_chip
            ? static_cast<int>(item.hwndItem == theme_mode_ ? 0
                               : item.hwndItem == theme_segment_[0] ? 1 : 2) ==
              theme_selection()
            : large_model() ? item.hwndItem == class_ai_
                            : item.hwndItem == class_traditional_;
        if (theme_chip) {
            draw_segment_option(dc, rect, text_of(item.hwndItem), font_, dpi_,
                                active, hot != FALSE, focused != FALSE,
                                card_background_);
            return TRUE;
        }
        FillRect(dc, &rect, card_background_);
        const COLORREF fill = active ? color_nav_active : color_input;
        fill_round_rect(dc, rect, px(logical_class_corner_radius, dpi_), fill,
                        color_line);
        draw_text(dc, text_of(item.hwndItem), rect, font_,
                  disabled ? color_text_faint
                           : active ? theme::color_text : color_text_dim,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return TRUE;
    }

    if (is_checkbox(item.hwndItem)) {
        // Most option rows sit on a card surface; the auto-copy option sits on
        // the page background instead.
        ui_controls::CheckboxStyle style;
        style.background =
            item.hwndItem == auto_copy_ ? theme::color_page : theme::color_card;
        style.fill = color_input;
        style.border = color_line;
        // The check mark keeps a neutral fill so nothing in the window turns
        // green on interaction; the caption still says what is on.
        style.checked_fill = theme::color_line_high;
        style.check = theme::color_text;
        style.text = color_text;
        style.disabled_text = color_text_faint;
        style.font = font_;
        style.dpi = dpi_;
        style.checked = checkbox_checked(item.hwndItem);
        style.enabled = !disabled;
        ui_controls::draw_checkbox(dc, rect, text_of(item.hwndItem), style,
                                   &settings_internal::fill_round_rect);
        return TRUE;
    }

    if (item.CtlID == id_text_copy || item.CtlID == id_text_clear) {
        FillRect(dc, &rect, card_background_);
        const wchar_t glyph = item.CtlID == id_text_copy ? ui_icons::copy
                                                         : ui_icons::delete_glyph;
        // Hover and keyboard focus brighten the glyph only: these buttons sit on
        // the card surface and must not paint a background of their own.
        const bool highlighted = hot || (item.itemState & ODS_FOCUS) != 0;
        draw_text(dc, std::wstring_view(&glyph, 1), rect,
                  action_icon_font_ ? action_icon_font_ : icon_font_,
                  disabled ? color_text_faint
                           : highlighted ? color_text : color_text_dim,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        return TRUE;
    }
    const COLORREF fill = disabled ? theme::color_line
                          : pressed ? theme::color_hover
                          : hot ? theme::color_line_high : color_input;
    FillRect(dc, &rect, card_background_);
    fill_round_rect(dc, rect, px(7, dpi_), fill, color_line);
    draw_text(dc, text_of(item.hwndItem), rect, font_,
              disabled ? color_text_faint : color_text_dim,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    return TRUE;
}


}  // namespace minitrans
