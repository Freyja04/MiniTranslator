#include "settings_window.hpp"
#include "settings_window_internal.hpp"

namespace minitrans {

using namespace settings_internal;

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
        for (const std::size_t index :
             {translation_index::class_label,
              translation_index::provider_label,
              translation_index::profile_label,
              translation_index::protocol_label,
              translation_index::key_label,
              translation_index::endpoint_label,
              translation_index::model_label,
              translation_index::target_label}) {
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
        for (const std::size_t index :
             {appearance_index::theme_label, appearance_index::copy_label}) {
            if (index < controls.size()) collect_label(controls[index]);
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
    const auto place_hint = [&](HWND hint, int logical_y, int logical_height,
                                bool full_width = true) {
        if (!hint) return;
        const auto style = GetWindowLongPtrW(hint, GWL_STYLE);
        SetWindowLongPtrW(hint, GWL_STYLE,
                          (style & ~static_cast<LONG_PTR>(SS_TYPEMASK)) | SS_LEFT);
        set_font(hint, small_font_);
        MoveWindow(hint, inner_left, px(logical_y, dpi_),
                   full_width ? available_width : label_width,
                   px(logical_height, dpi_), FALSE);
    };

    if (current_page_ == 0) {
        auto& c = page_controls_[0];
        if (c.size() > hotkey_index::selection_help) {
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
            const int block_height = logical_control_height + logical_hint_gap +
                                     logical_hint_height;
            const int stride = block_height + logical_row_gap;
            place_hotkey(c[hotkey_index::text_label], text_hotkey_, first_top);
            place_hotkey(c[hotkey_index::capture_label], capture_hotkey_,
                         first_top + stride);
            place_hotkey(c[hotkey_index::toggle_label], toggle_hotkey_,
                         first_top + stride * 2);
            MoveWindow(c[hotkey_index::selection_help], 0, 0, 0, 0, FALSE);
        }
    } else if (current_page_ == 1) {
        auto& c = page_controls_[1];
        const bool large_model = selected_index(translator_class_) == 1;
        int y = logical_content_top + logical_card_padding;
        if (!c.empty()) place(c[translation_index::class_label], translator_class_, y);
        y += logical_setting_row_height;
        if (large_model) {
            place(c[translation_index::profile_label], profile_select_, y);
            const int gap = px(6, dpi_);
            const int button_width = px(56, dpi_);
            const int select_width = std::max(
                px(110, dpi_),
                field_width - gap * 2 - button_width * 2);
            MoveWindow(profile_select_, field_left, px(y, dpi_),
                       select_width, px(260, dpi_), FALSE);
            MoveWindow(profile_add_, field_left + select_width + gap,
                       px(y, dpi_), button_width, control_height, FALSE);
            MoveWindow(profile_remove_,
                       field_left + select_width + gap * 2 + button_width,
                       px(y, dpi_), button_width, control_height, FALSE);
            y += logical_setting_row_height;
            place(c[translation_index::protocol_label], ai_protocol_, y);
            y += logical_setting_row_height;
        } else {
            place(c[translation_index::provider_label], provider_, y);
            y += logical_setting_row_height;
        }
        const auto place_row = [&](HWND label, HWND field, int top) {
            place(label, field, top);
            y = top + logical_setting_row_height;
        };
        const auto chosen = std::find_if(
            providers.begin(), providers.end(),
            [&](const ProviderInfo& value) { return value.id == loaded_provider_; });
        const bool has_key = large_model ||
            (chosen != providers.end() && chosen->kind != ProviderInfo::Kind::free);
        if (has_key) place_row(provider_key_label_, provider_key_, y);
        if (large_model) {
            place_row(provider_endpoint_label_, provider_endpoint_, y);
            place_row(provider_model_label_, provider_model_, y);
        }
        // 「获取模型」sits on its own row next to 「测试连接」 so that the model
        // combo keeps the full width of the field column.
        const int gap = px(6, dpi_);
        const int button_width = px(84, dpi_);
        if (large_model) {
            MoveWindow(model_refresh_, field_left, px(y, dpi_),
                       button_width, control_height, FALSE);
            MoveWindow(test_button_, field_left + button_width + gap,
                       px(y, dpi_), button_width, control_height, FALSE);
            MoveWindow(test_status_, field_left + (button_width + gap) * 2,
                       px(y, dpi_),
                       std::max(1, inner_right - field_left -
                                       (button_width + gap) * 2),
                       control_height, FALSE);
        } else {
            MoveWindow(test_button_, field_left, px(y, dpi_),
                       button_width, control_height, FALSE);
            MoveWindow(test_status_, field_left + button_width + gap,
                       px(y, dpi_),
                       std::max(1, inner_right - field_left - button_width - gap),
                       control_height, FALSE);
        }
        y += logical_setting_row_height;
        const int hint_top = y - logical_row_gap + logical_hint_gap;
        MoveWindow(provider_hint_, field_left, px(hint_top, dpi_),
                   field_width, px(34, dpi_), FALSE);
        const int main_bottom = hint_top + 34 + logical_card_padding;
        const int section_top = main_bottom + logical_section_gap;
        const int language_card_top =
            section_top + logical_section_height + logical_section_to_card;
        place(c[translation_index::target_label], chinese_target_,
              language_card_top + logical_card_padding);
    } else if (current_page_ == 2) {
        auto& c = page_controls_[2];
        if (c.size() > ocr_index::upscale_checkbox) {
            place(c[ocr_index::engine_label], ocr_engine_value_,
                  logical_ocr_engine_top);
            MoveWindow(ocr_engine_note_, field_left,
                       px(logical_ocr_engine_note_top, dpi_), field_width,
                       px(logical_hint_height, dpi_), FALSE);
            place(c[ocr_index::language_label], ocr_languages_,
                  logical_ocr_language_top);
            MoveWindow(c[ocr_index::installed_label], inner_left,
                       px(logical_ocr_installed_note_top, dpi_),
                       available_width, px(logical_hint_height, dpi_), FALSE);
            MoveWindow(ocr_upscale_, inner_left, px(logical_ocr_upscale_top, dpi_),
                       px(230, dpi_), control_height, FALSE);
        }
    } else if (current_page_ == 3) {
        auto& c = page_controls_[3];
        if (c.size() > appearance_index::copy_label) {
            const int first_top = logical_content_top + logical_card_padding;
            const int copy_top = first_top + logical_setting_row_height;
            place(c[appearance_index::theme_label], theme_mode_, first_top);
            place(c[appearance_index::copy_label], auto_copy_, copy_top);
        }
    } else if (current_page_ == 4) {
        auto& c = page_controls_[4];
        if (c.size() > other_index::copy_diagnostic) {
            const int first_top = logical_content_top + logical_card_padding;
            MoveWindow(autostart_, inner_left, px(first_top, dpi_),
                       px(230, dpi_), control_height, FALSE);
            const int separator_top = first_top + logical_control_height +
                                      logical_hint_gap + logical_hint_height +
                                      logical_row_gap;
            const int actions_top = separator_top + logical_row_gap;
            MoveWindow(restart_, inner_left, px(actions_top, dpi_),
                       px(84, dpi_), control_height, FALSE);
            MoveWindow(check_updates_, inner_left + px(84, dpi_) + px(10, dpi_),
                       px(actions_top, dpi_), px(84, dpi_), control_height, FALSE);
            MoveWindow(c[other_index::config_label], 0, 0, 0, 0, FALSE);

            const int config_section_top = actions_top + logical_control_height +
                                           logical_hint_gap + logical_hint_height +
                                           logical_card_padding + logical_section_gap;
            const int config_card_top = config_section_top + logical_section_height +
                                        logical_section_to_card;
            const int config_path_top = config_card_top + logical_card_padding;
            const int text_inset_x = px(9, dpi_);
            const int inset_top = std::max(
                1, px(logical_edit_inset_top, dpi_));
            const int inset_bottom = std::max(
                1, px(logical_edit_inset_bottom, dpi_));
            MoveWindow(config_path_, inner_left + text_inset_x,
                       px(config_path_top, dpi_) + inset_top,
                       std::max(1, available_width - text_inset_x * 2),
                       std::max(1, control_height - inset_top - inset_bottom), FALSE);
            set_rounded_control_region(
                config_path_,
                std::max(1, px(logical_edit_corner_radius, dpi_) - inset_top));
            const int open_top = config_path_top + logical_setting_row_height;
            MoveWindow(open_config_, inner_left, px(open_top, dpi_),
                       px(149, dpi_), control_height, FALSE);
            const int config_hint_top = open_top + logical_control_height +
                                        logical_hint_gap;
            place_hint(c[other_index::config_hint], config_hint_top, 40);

            const int diagnostic_section_top = config_hint_top + 40 +
                                               logical_card_padding +
                                               logical_section_gap;
            const int diagnostic_card_top = diagnostic_section_top +
                                            logical_section_height +
                                            logical_section_to_card;
            const int diagnostic_hint_top = diagnostic_card_top +
                                            logical_card_padding;
            const int diagnostic_command_top = diagnostic_hint_top + 40 +
                                               logical_row_gap;
            const int copy_width = px(72, dpi_);
            const int copy_gap = px(8, dpi_);
            const int command_frame_width =
                std::max(1, available_width - copy_width - copy_gap);
            MoveWindow(diagnostic_command_, inner_left + text_inset_x,
                       px(diagnostic_command_top, dpi_) + inset_top,
                       std::max(1, command_frame_width - text_inset_x * 2),
                       std::max(1, control_height - inset_top - inset_bottom), FALSE);
            set_rounded_control_region(
                diagnostic_command_,
                std::max(1, px(logical_edit_corner_radius, dpi_) - inset_top));
            MoveWindow(copy_diagnostic_,
                       inner_left + command_frame_width + copy_gap,
                       px(diagnostic_command_top, dpi_), copy_width,
                       control_height, FALSE);
        }
    } else if (current_page_ == 5) {
        const int toolbar_top =
            px(logical_content_top + logical_card_padding, dpi_);
        const int target_left = inner_left + px(68, dpi_);
        MoveWindow(text_target_, target_left, toolbar_top, px(166, dpi_),
                   px(318, dpi_), FALSE);

        int right = inner_right;
        MoveWindow(text_translate_, right - px(82, dpi_), toolbar_top,
                   px(82, dpi_), control_height, FALSE);
        right -= px(90, dpi_);
        MoveWindow(text_copy_, right - px(76, dpi_), toolbar_top,
                   px(76, dpi_), control_height, FALSE);
        right -= px(84, dpi_);
        MoveWindow(text_clear_, right - px(54, dpi_), toolbar_top,
                   px(54, dpi_), control_height, FALSE);

        MoveWindow(text_input_, card_left + px(13, dpi_),
                   px(logical_translation_input_panel_top +
                          logical_panel_header_height,
                      dpi_),
                   std::max(1, card_right - card_left - px(26, dpi_)),
                   px(118, dpi_), FALSE);
        MoveWindow(text_output_, card_left + px(13, dpi_),
                   px(logical_translation_output_panel_top +
                          logical_panel_header_height,
                      dpi_),
                   std::max(1, card_right - card_left - px(26, dpi_)),
                   px(150, dpi_), FALSE);
    }
}


}  // namespace minitrans
