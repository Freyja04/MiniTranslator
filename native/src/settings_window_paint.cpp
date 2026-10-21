#include "settings_window.hpp"
#include "settings_window_internal.hpp"

namespace minitrans {

using namespace settings_internal;

void SettingsWindow::paint() {
    const COLORREF accent = theme::accent;
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
    draw_text(dc, page_names[static_cast<std::size_t>(current_page_)],
              RECT{card_left, px(20, dpi_), card_right, px(48, dpi_)},
              title_font_, color_text, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_text(dc, page_subtitles[static_cast<std::size_t>(current_page_)],
              RECT{card_left, px(50, dpi_), card_right, px(72, dpi_)},
              small_font_, color_text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const auto card = [&](int top, int bottom) {
        fill_round_rect(dc, RECT{card_left, px(top, dpi_), card_right, px(bottom, dpi_)},
                        px(7, dpi_), color_card, color_line);
    };
    const auto separator = [&](int logical_y) {
        HPEN pen = CreatePen(PS_SOLID, 1, color_line);
        const auto previous = SelectObject(dc, pen);
        MoveToEx(dc, inner_left, px(logical_y, dpi_), nullptr);
        LineTo(dc, inner_right, px(logical_y, dpi_));
        SelectObject(dc, previous);
        DeleteObject(pen);
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
    // Key/description lists get a column sized to their longest key instead of
    // a fixed 132px block, so short keys do not leave a wide empty gap.
    const auto key_column_width = [&](const auto& rows) {
        const HGDIOBJ previous = SelectObject(dc, small_font_);
        int width = 0;
        for (const auto& row : rows) {
            SIZE measured{};
            GetTextExtentPoint32W(dc, row.first.data(),
                                  static_cast<int>(row.first.size()), &measured);
            width = std::max(width, static_cast<int>(measured.cx));
        }
        SelectObject(dc, previous);
        return std::clamp(width + px(14, dpi_),
                          px(84, dpi_), px(180, dpi_));
    };
    const auto draw_key_rows = [&](const auto& rows, int top, int step) {
        const int keys_width = key_column_width(rows);
        int y = top;
        for (const auto& row : rows) {
            draw_text(dc, row.first,
                      RECT{inner_left, px(y, dpi_),
                           inner_left + keys_width, px(y + step, dpi_)},
                      small_font_, color_text,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            draw_text(dc, row.second,
                      RECT{inner_left + keys_width + px(8, dpi_), px(y, dpi_),
                           inner_right, px(y + step, dpi_)},
                      small_font_, color_text_dim,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            y += step;
        }
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
        const int block_height = logical_control_height + logical_hint_gap +
                                 logical_hint_height;
        const int stride = block_height + logical_row_gap;
        const int main_bottom = first_top + stride * 3 - logical_row_gap +
                                logical_card_padding;
        card(logical_content_top, main_bottom);
        draw_text(dc, L"弹出聚焦式输入框；Esc 或再次按快捷键隐藏",
                  RECT{field_left,
                       px(first_top + logical_control_height + logical_hint_gap, dpi_),
                       inner_right,
                       px(first_top + logical_control_height + logical_hint_gap +
                              logical_hint_height, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, L"按下后拉框，松手立刻翻译",
                  RECT{field_left,
                       px(first_top + stride + logical_control_height +
                              logical_hint_gap, dpi_),
                       inner_right,
                       px(first_top + stride + logical_control_height +
                              logical_hint_gap + logical_hint_height, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, L"同一个键来回切：译文开着就收进托盘，收着就叫回来",
                  RECT{field_left,
                       px(first_top + stride * 2 + logical_control_height +
                              logical_hint_gap, dpi_),
                       inner_right,
                       px(first_top + stride * 2 + logical_control_height +
                              logical_hint_gap + logical_hint_height, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        const int section_top = main_bottom + logical_section_gap;
        section(L"框选时", section_top);
        const int selection_card_top =
            section_top + logical_section_height + logical_section_to_card;
        const int selection_rows_top = selection_card_top + logical_card_padding;
        card(selection_card_top, selection_rows_top + 36 + logical_card_padding);
        const std::array<std::pair<std::wstring_view, std::wstring_view>, 2>
            selection_rows{{
                {L"拖动鼠标", L"画出要翻译的区域，松手立刻识别并翻译"},
                {L"Esc / 右键", L"取消这次框选"},
            }};
        draw_key_rows(selection_rows, selection_rows_top, 18);
    } else if (current_page_ == 1) {
        const bool large_model = selected_index(translator_class_) == 1;
        const auto chosen = std::find_if(
            providers.begin(), providers.end(),
            [&](const ProviderInfo& value) { return value.id == loaded_provider_; });
        const bool has_key = large_model ||
            (chosen != providers.end() && chosen->kind != ProviderInfo::Kind::free);
        // Mirror the row arithmetic in layout_controls so the card's top and
        // bottom breathing room stay identical.
        int last_top = logical_content_top + logical_card_padding;
        last_top += logical_setting_row_height;  // class
        last_top += logical_setting_row_height;  // provider/profile
        if (large_model) last_top += logical_setting_row_height;
        if (has_key) last_top += logical_setting_row_height;
        if (large_model) last_top += logical_setting_row_height * 2;
        const int hint_top = last_top + logical_control_height + logical_hint_gap;
        const int main_bottom = hint_top + 34 + logical_card_padding;
        card(logical_content_top, main_bottom);
        const int section_y = main_bottom + logical_section_gap;
        section(L"语言", section_y);
        const int language_card_top =
            section_y + logical_section_height + logical_section_to_card;
        card(language_card_top, language_card_top + logical_card_padding +
                                      logical_control_height + logical_card_padding);
    } else if (current_page_ == 2) {
        card(logical_content_top, logical_ocr_main_bottom);
        section(L"安装其他语言", logical_ocr_language_section_top);
        card(logical_ocr_language_card_top,
             logical_ocr_language_card_bottom);
        draw_text(dc,
                  L"需要日语、韩语等语言时，到 Windows 设置添加该语言，再进入语言选项安装光学字符识别，装好后重启本程序即可。",
                  RECT{inner_left, px(logical_ocr_language_note_top, dpi_),
                       inner_right,
                       px(logical_ocr_language_note_top +
                              logical_ocr_language_note_height,
                          dpi_)},
                  small_font_, color_text_dim, DT_LEFT | DT_TOP | DT_WORDBREAK);
    } else if (current_page_ == 3) {
        const int first_top = logical_content_top + logical_card_padding;
        const int copy_top = first_top + logical_setting_row_height;
        const int main_bottom = copy_top + logical_control_height +
                                logical_card_padding;
        card(logical_content_top, main_bottom);
    } else if (current_page_ == 4) {
        const int first_top = logical_content_top + logical_card_padding;
        const int separator_top = first_top + logical_control_height +
                                  logical_hint_gap + logical_hint_height +
                                  logical_row_gap;
        const int actions_top = separator_top + logical_row_gap;
        const int main_bottom = actions_top + logical_control_height +
                                logical_hint_gap + logical_hint_height +
                                logical_card_padding;
        card(logical_content_top, main_bottom);
        separator(separator_top);
        draw_text(dc, L"装完之后每次开机自动到托盘待命，不会弹窗口。",
                  RECT{inner_left,
                       px(first_top + logical_control_height + logical_hint_gap, dpi_),
                       inner_right,
                       px(first_top + logical_control_height + logical_hint_gap +
                              logical_hint_height, dpi_)},
                  small_font_, color_text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, L"刚给 Windows 装完新的识别语言包，重启一下才会生效。",
                  RECT{inner_left,
                       px(actions_top + logical_control_height + logical_hint_gap,
                          dpi_),
                       inner_right,
                       px(actions_top + logical_control_height + logical_hint_gap +
                              logical_hint_height, dpi_)},
                  small_font_, color_text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const int config_section_top = main_bottom + logical_section_gap;
        section(L"配置文件", config_section_top);
        const int config_card_top = config_section_top + logical_section_height +
                                    logical_section_to_card;
        const int config_path_top = config_card_top + logical_card_padding;
        const int open_top = config_path_top + logical_setting_row_height;
        const int config_hint_top = open_top + logical_control_height +
                                    logical_hint_gap;
        const int config_bottom = config_hint_top + 40 + logical_card_padding;
        card(config_card_top, config_bottom);
        const RECT config_frame{
            inner_left, px(config_path_top, dpi_), inner_right,
            px(config_path_top + logical_control_height, dpi_)};
        fill_round_rect(dc, config_frame, px(logical_edit_corner_radius, dpi_),
                        color_input, color_line);

        const int diagnostic_section_top = config_bottom + logical_section_gap;
        section(L"故障排查", diagnostic_section_top);
        const int diagnostic_card_top = diagnostic_section_top +
                                        logical_section_height +
                                        logical_section_to_card;
        const int diagnostic_note_top = diagnostic_card_top + logical_card_padding;
        const int diagnostic_command_top =
            diagnostic_note_top + 40 + logical_row_gap;
        card(diagnostic_card_top,
             diagnostic_command_top + logical_control_height + logical_card_padding);
        const auto command_right =
            inner_right - px(80, dpi_);
        const RECT diagnostic_frame{
            inner_left, px(diagnostic_command_top, dpi_), command_right,
            px(diagnostic_command_top + logical_control_height, dpi_)};
        fill_round_rect(dc, diagnostic_frame, px(logical_edit_corner_radius, dpi_),
                        color_input, color_line);
        draw_text(dc,
                  L"命令行跑一次自检，会逐项检查 OCR 语言包 → 文字识别 → 译文排版 → 翻译接口 → 全局快捷键，一眼看出断在哪一环：",
                  RECT{inner_left, px(diagnostic_note_top, dpi_), inner_right,
                       px(diagnostic_note_top + 40, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_TOP | DT_WORDBREAK);
    } else if (current_page_ == 5) {
        const int toolbar_top = logical_content_top;
        card(toolbar_top, toolbar_top + logical_control_height +
                              logical_card_padding * 2);
        draw_text(dc, L"目标语言",
                  RECT{inner_left, px(toolbar_top + logical_card_padding, dpi_),
                       inner_left + px(62, dpi_),
                       px(toolbar_top + logical_card_padding +
                              logical_control_height, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        const RECT input_panel{
            card_left, px(logical_translation_input_panel_top, dpi_), card_right,
            px(logical_translation_input_panel_bottom, dpi_)};
        const RECT output_panel{
            card_left, px(logical_translation_output_panel_top, dpi_), card_right,
            px(logical_translation_output_panel_bottom, dpi_)};
        fill_round_rect(dc, input_panel, px(7, dpi_), color_input,
                        GetFocus() == text_input_ ? accent : color_line);
        fill_round_rect(dc, output_panel, px(7, dpi_), color_input,
                        GetFocus() == text_output_ ? accent : color_line);
        draw_text(dc, L"原文",
                  RECT{input_panel.left + px(13, dpi_), input_panel.top,
                       input_panel.left + px(80, dpi_), input_panel.top + px(30, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, L"译文",
                  RECT{output_panel.left + px(13, dpi_), output_panel.top,
                       output_panel.left + px(80, dpi_), output_panel.top + px(30, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        if (text_session_) {
            const auto counter = std::to_wstring(text_session_->character_count()) +
                                 L" / 5000";
            draw_text(dc, counter,
                      RECT{input_panel.right - px(120, dpi_), input_panel.top,
                           input_panel.right - px(13, dpi_), input_panel.top + px(30, dpi_)},
                      small_font_,
                      text_session_->state() == TextTranslationState::too_long
                          ? color_bad : color_text_faint,
                      DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
            const bool error = text_session_->state() == TextTranslationState::error ||
                               text_session_->state() == TextTranslationState::too_long;
            draw_text(dc, text_translation_status(*text_session_),
                      RECT{card_left + px(2, dpi_),
                           px(logical_translation_output_panel_bottom +
                                  logical_hint_gap,
                              dpi_),
                           card_right,
                           px(logical_translation_output_panel_bottom +
                                  logical_hint_gap + logical_hint_height,
                              dpi_)},
                      small_font_, error ? color_bad : color_text_dim,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        }
        draw_text(dc, L"自动选择会把中文译成设置的目标语言，其他语言译成简体中文。",
                  RECT{card_left + px(2, dpi_),
                       px(logical_translation_output_panel_bottom +
                              logical_hint_gap + logical_hint_height + 2,
                          dpi_),
                       card_right,
                       px(logical_translation_output_panel_bottom +
                              logical_hint_gap + logical_hint_height * 2 + 2,
                          dpi_)},
                  small_font_, color_text_faint,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    const HWND focused = GetFocus();
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
                        focused == control ? accent : color_line);
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

    if (item.CtlType == ODT_COMBOBOX) {
        const bool list_item = (item.itemState & ODS_COMBOBOXEDIT) == 0;
        const bool selected = list_item && (item.itemState & ODS_SELECTED) != 0;
        FillRect(dc, &rect, input_background_);
        if (selected) {
            RECT highlight = rect;
            InflateRect(&highlight, -px(4, dpi_), -px(2, dpi_));
            const COLORREF selected_fill = blend_color(color_input, accent, 16);
            const COLORREF selected_border = blend_color(color_line_high, accent, 34);
            fill_round_rect(dc, highlight, px(5, dpi_), selected_fill, selected_border);
            RECT marker{highlight.left + px(3, dpi_), highlight.top + px(6, dpi_),
                        highlight.left + px(5, dpi_), highlight.bottom - px(6, dpi_)};
            HBRUSH marker_brush = CreateSolidBrush(accent);
            FillRect(dc, &marker, marker_brush);
            DeleteObject(marker_brush);
        }
        std::wstring value;
        if (item.itemID != static_cast<UINT>(-1)) {
            const LRESULT length = SendMessageW(item.hwndItem, CB_GETLBTEXTLEN, item.itemID, 0);
            if (length >= 0) {
                value.resize(static_cast<std::size_t>(length) + 1);
                SendMessageW(item.hwndItem, CB_GETLBTEXT, item.itemID,
                             reinterpret_cast<LPARAM>(value.data()));
                value.resize(static_cast<std::size_t>(length));
            }
        } else {
            value = text_of(item.hwndItem);
        }
        rect.left += px(list_item ? 13 : 9, dpi_);
        rect.right -= px(list_item ? 9 : 25, dpi_);
        if (!list_item) {
            OffsetRect(&rect, 0, px(logical_field_text_offset_y, dpi_));
        }
        draw_text(dc, value, rect, font_, disabled ? color_text_faint : color_text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (!list_item) {
            HPEN pen = CreatePen(PS_SOLID, px(2, dpi_), color_text_dim);
            const auto previous = SelectObject(dc, pen);
            const int x = item.rcItem.right - px(14, dpi_);
            const int y = (item.rcItem.top + item.rcItem.bottom) / 2;
            MoveToEx(dc, x - px(4, dpi_), y - px(2, dpi_), nullptr);
            LineTo(dc, x, y + px(2, dpi_));
            LineTo(dc, x + px(4, dpi_), y - px(2, dpi_));
            SelectObject(dc, previous);
            DeleteObject(pen);
        }
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

    if (is_checkbox(item.hwndItem)) {
        FillRect(dc, &rect, card_background_);
        const int size = px(16, dpi_);
        const int top = rect.top + (rect.bottom - rect.top - size) / 2;
        RECT box{rect.left, top, rect.left + size, top + size};
        const bool checked = checkbox_checked(item.hwndItem);
        fill_round_rect(dc, box, px(4, dpi_), checked ? accent : color_input,
                        checked ? accent : color_line);
        if (checked) {
            HPEN pen = CreatePen(PS_SOLID, px(2, dpi_), theme::accent_foreground);
            const auto previous = SelectObject(dc, pen);
            MoveToEx(dc, box.left + px(4, dpi_), box.top + px(8, dpi_), nullptr);
            LineTo(dc, box.left + px(7, dpi_), box.top + px(11, dpi_));
            LineTo(dc, box.left + px(13, dpi_), box.top + px(5, dpi_));
            SelectObject(dc, previous);
            DeleteObject(pen);
        }
        RECT text_rect{box.right + px(9, dpi_), rect.top, rect.right, rect.bottom};
        draw_text(dc, text_of(item.hwndItem), text_rect, font_,
                  disabled ? color_text_faint : color_text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return TRUE;
    }

    const COLORREF fill = disabled ? theme::color_line
                          : pressed ? theme::color_hover
                          : hot ? theme::color_line_high : theme::color_card;
    FillRect(dc, &rect, card_background_);
    fill_round_rect(dc, rect, px(6, dpi_), fill, focused ? accent : color_line_high);
    draw_text(dc, text_of(item.hwndItem), rect, font_,
              disabled ? color_text_faint : color_text,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    return TRUE;
}


}  // namespace minitrans
