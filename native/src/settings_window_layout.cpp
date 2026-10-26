#include "settings_window.hpp"
#include "settings_window_internal.hpp"

#include "settings_provider.hpp"

namespace minitrans {

using namespace settings_internal;

int SettingsWindow::ai_list_item_count() const noexcept {
    if (!ai_list_ || !IsWindow(ai_list_)) return 0;
    return static_cast<int>(SendMessageW(ai_list_, LB_GETCOUNT, 0, 0));
}

void SettingsWindow::layout_translation_page(int inner_left, int inner_right,
                                             int field_left, int field_width,
                                             int control_height) {
    auto& controls = page_controls_[1];
    const auto label_for = [&](std::size_t index) {
        return index < controls.size() ? controls[index] : nullptr;
    };
    // Everything below is in physical pixels and handed to the paint pass
    // unchanged; scaling it twice would move the card away from the controls it
    // frames.
    TranslationGeometry geometry;
    geometry.large_model = large_model();
    const int row_gap = px(logical_row_gap, dpi_);
    const int stride = control_height + row_gap;
    const int card_top = px(logical_content_top, dpi_);
    const int row_top = px(logical_content_top + logical_card_padding, dpi_);
    const int label_width =
        std::max(1, field_left - px(logical_label_gap, dpi_) - inner_left);
    const auto place_label = [&](HWND label, int top, int width) {
        if (!label) return;
        const auto style = GetWindowLongPtrW(label, GWL_STYLE);
        // The captions keep their style, so a layout pass that does not need to
        // change it must not write one: a style write repaints the static.
        const auto wanted = (style & ~static_cast<LONG_PTR>(SS_TYPEMASK)) |
                            SS_LEFT | SS_CENTERIMAGE;
        if (style != wanted) SetWindowLongPtrW(label, GWL_STYLE, wanted);
        MoveWindow(label, inner_left, top, std::max(1, width), control_height, FALSE);
    };
    const auto place_field = [&](HWND label, HWND field, int top) {
        place_label(label, top, label_width);
        if (!field) return;
        wchar_t type[24]{};
        GetClassNameW(field, type, static_cast<int>(std::size(type)));
        const bool edit = _wcsicmp(type, L"Edit") == 0;
        const int inset_x = edit ? std::max(1, px(logical_edit_inset_x, dpi_)) : 0;
        const int inset_top = edit ? std::max(1, px(logical_edit_inset_top, dpi_)) : 0;
        const int inset_bottom =
            edit ? std::max(1, px(logical_edit_inset_bottom, dpi_)) : 0;
        MoveWindow(field, field_left + inset_x, top + inset_top,
                   std::max(1, field_width - inset_x * 2),
                   std::max(1, control_height - inset_top - inset_bottom), FALSE);
    };

    // 类别: two owner-drawn buttons that read as one segmented switch, both
    // starting at the card's inner edge. 添加 rides to the right of the
    // large-model half and is only enabled on that branch; the caption label
    // is gone because the rows explain themselves.
    const int half = std::max(1, (field_width - px(logical_provider_gap, dpi_)) / 2);
    MoveWindow(class_traditional_, inner_left, row_top, half, control_height,
               FALSE);
    MoveWindow(class_ai_, inner_left + half + px(logical_provider_gap, dpi_),
               row_top, half, control_height, FALSE);
    if (HWND stale_label = label_for(translation_index::class_label)) {
        ShowWindow(stale_label, SW_HIDE);
    }
    const int add_width = px(logical_ai_add_width, dpi_);
    // 添加 hugs the card's right edge so the row toggles below line up with
    // its circle at the same column.
    MoveWindow(ai_add_, inner_right - add_width, row_top, add_width,
               control_height, FALSE);
    EnableWindow(ai_add_, large_model() ? TRUE : FALSE);

    int next_top = row_top + stride;
    int bottom = row_top + control_height;
    // Both branches show the same list control: endpoint rows on the
    // large-model side, provider rows (toggle, and 编辑 for DeepL) on the
    // traditional side.
    ShowWindow(ai_list_caption_, SW_HIDE);
    geometry.separators[geometry.separator_count++] = next_top - row_gap / 2;

    const int rows = ai_list_item_count();
    const int visible = std::min(rows, logical_ai_list_max_rows);
    const int row_height = px(settings_ai_list::logical_row_height(), dpi_);
    const int band_height = std::max(1, visible) * row_height;
    ShowWindow(ai_list_, rows > 0 ? SW_SHOW : SW_HIDE);
    if (rows > 0) {
        MoveWindow(ai_list_, inner_left, next_top,
                   std::max(1, inner_right - inner_left), band_height, FALSE);
        bottom = next_top + band_height;
    } else {
        // With no rows the list is hidden and one empty row is kept so the
        // page does not jump when the first entry appears.
        bottom = next_top + control_height;
    }

    geometry.card_top = card_top;
    // The row buttons say everything, so the card ends with the last row
    // instead of an empty hint band on either branch.
    geometry.card_bottom = bottom + px(logical_card_padding, dpi_);
    translation_ = geometry;
}

void SettingsWindow::layout_controls() {
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right;
    const int height = client.bottom;
    const int sidebar = px(logical_sidebar_width, dpi_);
    for (std::size_t index = 0; index < navigation_.size(); ++index) {
        MoveWindow(navigation_[index], px(12, dpi_), px(18 + static_cast<int>(index) * 42, dpi_),
                   std::max(1, sidebar - px(24, dpi_)), px(38, dpi_), FALSE);
    }
    const int status_top = height - px(logical_status_bottom_margin +
                                           logical_status_height,
                                       dpi_);
    MoveWindow(status_, sidebar + px(logical_page_margin, dpi_),
               status_top,
               std::max(1, width - sidebar - px(logical_page_margin * 2, dpi_)),
               px(logical_status_height, dpi_), FALSE);

    const int card_left = sidebar + px(logical_page_margin, dpi_);
    const int card_right = width - px(logical_page_margin, dpi_);
    const int inner_left = card_left + px(18, dpi_);
    const int inner_right = card_right - px(18, dpi_);
    const int available_width = std::max(1, inner_right - inner_left);
    const int label_gap = px(logical_label_gap, dpi_);
    // Size the caption column from the labels actually shown on this page.
    // A fixed, generous column made short captions float towards the middle of
    // the card and left a large blank gutter on the left.
    std::vector<HWND> labels;
    const auto collect_label = [&](HWND label) {
        if (!label ||
            (GetWindowLongPtrW(label, GWL_STYLE) & WS_VISIBLE) == 0) {
            return;
        }
        labels.push_back(label);
    };
    if (current_page_ == 0 &&
        page_controls_[0].size() > hotkey_index::toggle_label) {
        collect_label(page_controls_[0][hotkey_index::text_label]);
        collect_label(page_controls_[0][hotkey_index::capture_label]);
        collect_label(page_controls_[0][hotkey_index::toggle_label]);
    } else if (current_page_ == 1) {
        const auto& controls = page_controls_[1];
        // The endpoint-list caption spans the card instead of driving the label
        // column, so only the row captions size the caption column.
        for (const std::size_t index :
             {translation_index::class_label,
              translation_index::provider_label,
              translation_index::key_label}) {
            if (index < controls.size()) collect_label(controls[index]);
        }
    } else if (current_page_ == 2) {
        const auto& controls = page_controls_[2];
        if (controls.size() > ocr_index::language_label) {
            collect_label(controls[ocr_index::engine_label]);
            collect_label(controls[ocr_index::language_label]);
        }
    } else if (current_page_ == 3) {
        const auto& controls = page_controls_[3];
        if (controls.size() > other_index::theme_label) {
            collect_label(controls[other_index::theme_label]);
        }
    }
    int label_text_width = px(logical_field_min_offset, dpi_) - label_gap;
    if (!labels.empty()) {
        HDC dc = GetDC(window_);
        if (dc) {
            const HGDIOBJ previous = SelectObject(dc, font_);
            for (const HWND label : labels) {
                const std::wstring caption = text_of(label);
                SIZE measured{};
                GetTextExtentPoint32W(dc, caption.c_str(),
                                      static_cast<int>(caption.size()), &measured);
                label_text_width = std::max(label_text_width,
                                            static_cast<int>(measured.cx));
            }
            SelectObject(dc, previous);
            ReleaseDC(window_, dc);
        }
    }
    field_offset_ = std::clamp(
        label_text_width + label_gap,
        px(logical_field_min_offset, dpi_),
        px(logical_field_max_offset, dpi_));
    const int label_width = std::max(1, field_offset_ - label_gap);
    const int field_left = inner_left + field_offset_;
    const int field_width = std::max(1, inner_right - field_left);
    const int control_height = px(logical_control_height, dpi_);

    const auto move_field = [&](HWND field, int logical_top, int logical_width = -1) {
        if (!field) return;
        wchar_t type[24]{};
        GetClassNameW(field, type, static_cast<int>(std::size(type)));
        const bool combo = _wcsicmp(type, L"ComboBox") == 0;
        const bool edit = _wcsicmp(type, L"Edit") == 0;
        const int requested = logical_width < 0 ? field_width : px(logical_width, dpi_);
        const int field_top = px(logical_top, dpi_);
        const int inset_x = edit
            ? std::max(1, px(logical_edit_inset_x, dpi_)) : 0;
        const int inset_top = edit
            ? std::max(1, px(logical_edit_inset_top, dpi_)) : 0;
        const int inset_bottom = edit
            ? std::max(1, px(logical_edit_inset_bottom, dpi_)) : 0;
        MoveWindow(field, field_left + inset_x, field_top + inset_top,
                   std::max(1, std::min(field_width, requested) - inset_x * 2),
                   combo ? px(260, dpi_)
                         : std::max(1, control_height - inset_top - inset_bottom), FALSE);
    };
    const auto place = [&](HWND label, HWND field, int logical_top, int logical_width = -1) {
        if (!label || !field) return;
        const auto label_style = GetWindowLongPtrW(label, GWL_STYLE);
        SetWindowLongPtrW(label, GWL_STYLE,
                          (label_style & ~static_cast<LONG_PTR>(SS_TYPEMASK)) |
                          SS_LEFT | SS_CENTERIMAGE);
        const int label_top = px(logical_top, dpi_);
        MoveWindow(label, inner_left, label_top, label_width,
                   control_height, FALSE);
        move_field(field, logical_top, logical_width);
    };

    if (current_page_ == 0) {
        auto& c = page_controls_[0];
        if (c.size() > hotkey_index::toggle_edit) {
            const auto place_hotkey = [&](HWND label, HWND edit, int logical_top) {
                const auto label_style = GetWindowLongPtrW(label, GWL_STYLE);
                SetWindowLongPtrW(label, GWL_STYLE,
                                  (label_style & ~static_cast<LONG_PTR>(SS_TYPEMASK)) |
                                      SS_LEFT | SS_CENTERIMAGE);
                MoveWindow(label, inner_left, px(logical_top, dpi_), label_width,
                           px(34, dpi_), FALSE);
                const int inset_x = std::max(
                    1, px(logical_edit_inset_x, dpi_));
                const int inset_top = std::max(
                    1, px(logical_edit_inset_top, dpi_));
                const int inset_bottom = std::max(
                    1, px(logical_edit_inset_bottom, dpi_));
                MoveWindow(edit, field_left + inset_x,
                           px(logical_top, dpi_) + inset_top,
                           std::max(1, field_width - inset_x * 2),
                           px(34, dpi_) - inset_top - inset_bottom, FALSE);
            };
            const int first_top = logical_content_top + logical_card_padding;
            const int block_height = logical_control_height;
            const int stride = block_height + logical_row_gap;
            place_hotkey(c[hotkey_index::text_label], text_hotkey_, first_top);
            place_hotkey(c[hotkey_index::capture_label], capture_hotkey_,
                         first_top + stride);
            place_hotkey(c[hotkey_index::toggle_label], toggle_hotkey_,
                         first_top + stride * 2);
        }
    } else if (current_page_ == 1) {
        layout_translation_page(inner_left, inner_right, field_left, field_width,
                                control_height);
    } else if (current_page_ == 2) {
        auto& c = page_controls_[2];
        if (c.size() > ocr_index::upscale_checkbox) {
            place(c[ocr_index::engine_label], ocr_engine_value_,
                  logical_ocr_engine_top);
            place(c[ocr_index::language_label], ocr_languages_,
                  logical_ocr_language_top);
            place(c[ocr_index::target_label], translation_target_,
                  logical_ocr_target_top);
            MoveWindow(c[ocr_index::installed_label], inner_left,
                       px(logical_ocr_installed_note_top, dpi_),
                       available_width, px(logical_hint_height, dpi_), FALSE);
            MoveWindow(ocr_upscale_, inner_left, px(logical_ocr_upscale_top, dpi_),
                       px(230, dpi_), control_height, FALSE);
        }
    } else if (current_page_ == 3) {
        auto& c = page_controls_[3];
        if (c.size() > other_index::check_updates) {
            const int first_top = logical_content_top + logical_card_padding;
            const int autostart_top = first_top + logical_setting_row_height;
            const int separator_top =
                autostart_top + logical_control_height + logical_row_gap;
            const int actions_top = separator_top + logical_row_gap;
            // 主题 is one segmented row: measure the three captions, then lay
            // the chips edge to edge inside the field column.
            if (c[other_index::theme_label]) {
                const auto label_style =
                    GetWindowLongPtrW(c[other_index::theme_label], GWL_STYLE);
                SetWindowLongPtrW(c[other_index::theme_label], GWL_STYLE,
                                  (label_style &
                                   ~static_cast<LONG_PTR>(SS_TYPEMASK)) |
                                      SS_LEFT | SS_CENTERIMAGE);
                MoveWindow(c[other_index::theme_label], inner_left,
                           px(first_top, dpi_), label_width, control_height,
                           FALSE);
            }
            {
                // The chips sit in the field column with a breathing gap and a
                // floor width, so short captions like 主题 never crowd them.
                const std::array<std::wstring_view, 3> captions{
                    theme_modes[0], theme_modes[1], theme_modes[2],
                };
                std::vector<int> widths;
                if (HDC dc = GetDC(window_)) {
                    widths = segment_widths(dc, font_, captions, dpi_);
                    ReleaseDC(window_, dc);
                }
                if (widths.size() == 3) {
                    const int gap = px(8, dpi_);
                    const int min_width = px(96, dpi_);
                    for (int& chip_width : widths) {
                        chip_width = std::max(chip_width, min_width);
                    }
                    int x = field_left;
                    const HWND chips[3]{
                        theme_mode_, theme_segment_[0], theme_segment_[1],
                    };
                    for (int chip = 0; chip < 3; ++chip) {
                        MoveWindow(chips[chip], x, px(first_top, dpi_),
                                   widths[chip], control_height, FALSE);
                        x += widths[chip] + gap;
                    }
                }
            }
            MoveWindow(autostart_, inner_left, px(autostart_top, dpi_),
                       px(230, dpi_), control_height, FALSE);
            MoveWindow(diagnostics_, inner_left, px(actions_top, dpi_),
                       px(84, dpi_), control_height, FALSE);
            MoveWindow(check_updates_, inner_left + px(84, dpi_) + px(10, dpi_),
                       px(actions_top, dpi_), px(84, dpi_), control_height, FALSE);
            const int status_left = inner_left + px(84, dpi_) + px(10, dpi_) +
                                    px(84, dpi_) + px(12, dpi_);
            MoveWindow(update_status_, status_left, px(actions_top, dpi_),
                       std::max(1, inner_right - status_left), control_height, FALSE);
        }
    } else if (current_page_ == 4) {
        // The language row takes the left edge and the capture button moves to
        // the right edge, mirroring the quick translation window's header
        // while keeping both on the page background above one shared card.
        const int toolbar_top = px(logical_translation_toolbar_top, dpi_);
        MoveWindow(text_capture_, card_right - control_height, toolbar_top,
                   control_height, control_height, FALSE);
        const int pair_width = px(126, dpi_);
        const int pair_height = px(318, dpi_);
        const int swap_width = px(30, dpi_);
        const int gap = px(8, dpi_);
        // Align the language row with the card's left edge, matching the quick
        // translation window where the header and the card share one margin.
        const int toolbar_left = card_left;
        MoveWindow(text_source_, toolbar_left, toolbar_top, pair_width,
                   pair_height, FALSE);
        MoveWindow(text_swap_, toolbar_left + pair_width + gap, toolbar_top,
                   swap_width, control_height, FALSE);
        MoveWindow(text_target_,
                   toolbar_left + pair_width + gap + swap_width + gap,
                   toolbar_top, pair_width, pair_height, FALSE);

        const int inset = px(logical_translation_inset, dpi_);
        const int edit_left = card_left + inset;
        const int edit_width = std::max(1, card_right - inset - edit_left);
        const int label_height = px(logical_translation_label_height, dpi_);
        const int body_top = px(logical_translation_body_top, dpi_);
        const int divider = px(logical_translation_divider, dpi_);
        const int body_bottom = px(logical_translation_body_bottom, dpi_);
        const int input_top = body_top + label_height;
        const int output_top = divider + label_height;
        MoveWindow(text_input_, edit_left, input_top, edit_width,
                   std::max(1, divider - input_top), FALSE);
        // Keep the card's bottom border visible: a child window that ends on the
        // border row paints over it with the card fill.
        MoveWindow(text_output_, edit_left, output_top, edit_width,
                   std::max(1, body_bottom - output_top - 1), FALSE);
        MoveWindow(auto_copy_, card_left,
                   px(logical_translation_auto_copy_top, dpi_),
                   px(240, dpi_), control_height, FALSE);

        int caption_advance = 0;
        if (HDC dc = GetDC(window_)) {
            const HGDIOBJ previous = SelectObject(dc, small_font_);
            for (const wchar_t* caption : {L"原文", L"译文"}) {
                SIZE measured{};
                if (GetTextExtentPoint32W(dc, caption, 2, &measured)) {
                    caption_advance =
                        std::max(caption_advance, static_cast<int>(measured.cx));
                }
            }
            SelectObject(dc, previous);
            ReleaseDC(window_, dc);
        }
        const int action_width = px(26, dpi_);
        const int action_height = px(22, dpi_);
        const int action_left = edit_left + caption_advance + px(6, dpi_);
        const int action_offset = (label_height - action_height) / 2;
        MoveWindow(text_clear_, action_left, body_top + action_offset,
                   action_width, action_height, FALSE);
        MoveWindow(text_copy_, action_left, divider + action_offset,
                   action_width, action_height, FALSE);
    }
}


}  // namespace minitrans
