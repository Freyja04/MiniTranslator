#include "settings_window.hpp"
#include "settings_window_internal.hpp"

#include "diagnostics.hpp"
#include "hotkey.hpp"
#include "language.hpp"
#include "settings_provider.hpp"
#include "resource.h"
#include "theme.hpp"
#include "translator.hpp"
#include "ui_draw.hpp"
#include "ui_controls.hpp"
#include "ui_icons.hpp"
#include "util.hpp"

#include <commctrl.h>
#include <d2d1.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Globalization.h>
#include <winrt/Windows.Media.Ocr.h>

#include <algorithm>
#include <array>
#include <memory>

namespace minitrans {

using namespace settings_internal;

LRESULT SettingsWindow::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE: create_controls(); return 0;
    case WM_SIZE:
        layout_controls();
        if (wparam != SIZE_MINIMIZED) {
            RedrawWindow(window_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN);
        }
        return 0;
    case WM_GETMINMAXINFO: {
        RECT minimum{0, 0, px(760, dpi_), px(logical_window_height, dpi_)};
        AdjustWindowRectExForDpi(
            &minimum,
            static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE)), FALSE,
            static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_EXSTYLE)),
            static_cast<UINT>(dpi_));
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        limits->ptMinTrackSize.x = minimum.right - minimum.left;
        limits->ptMinTrackSize.y = minimum.bottom - minimum.top;
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wparam);
        const auto* suggested = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left, suggested->bottom - suggested->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        create_theme_resources();
        for (const auto button : navigation_) set_font(button, font_);
        set_font(status_, small_font_);
        for (auto& controls : page_controls_) {
            for (const auto control : controls) {
                set_font(control, font_);
                apply_control_theme(control);
            }
        }
        // The hint labels use a smaller font than the rest of the page, so the
        // rebuild above has to put that font back instead of leaving them
        // stretched to the page font.
        set_font(test_status_, small_font_);
        set_font(provider_hint_, small_font_);
        layout_controls();
        RedrawWindow(window_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
        return 0;
    }
    case WM_NCACTIVATE:
        // Windows 10 does not support per-window caption colors and paints an
        // inactive dark caption in gray. Keep the active caption palette so
        // clicking the desktop does not visibly change the settings title bar.
        return DefWindowProcW(window_, message, TRUE, lparam);
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notification = HIWORD(wparam);
        const HWND source = reinterpret_cast<HWND>(lparam);
        if (id >= id_nav_first && id <= id_nav_last && notification == BN_CLICKED) {
            ScopedRedrawPause redraw_pause(window_);
            save_values();
            show_page(navigation_page_order[static_cast<std::size_t>(id - id_nav_first)]);
            return 0;
        }
        if (notification == EN_SETFOCUS || notification == EN_KILLFOCUS) {
            InvalidateRect(window_, nullptr, FALSE);
        }
        if (notification == CBN_DROPDOWN && source) {
            set_combo_edit_redraw(source, false);
            theme::apply_control_theme(source);
            style_combo_popup(source, dpi_, font_);
            clear_combo_edit_selection(source);
            InvalidateRect(source, nullptr, FALSE);
        }
        if (notification == CBN_CLOSEUP && source) {
            clear_combo_edit_selection(source);
            align_combo_edit(source, dpi_);
            set_combo_edit_redraw(source, true);
            InvalidateRect(source, nullptr, FALSE);
        }
        // The segmented theme row picks the mode on click, the way the
        // drop-down applied it on selection. save_values() applies the mode
        // and the settings-changed callback refreshes the other windows; the
        // DWM caption needs one explicit rebuild afterwards.
        if (notification == BN_CLICKED && !loading_ &&
            (source == theme_mode_ || source == theme_segment_[0] ||
             source == theme_segment_[1])) {
            const int chosen = source == theme_mode_ ? 0
                               : source == theme_segment_[0] ? 1 : 2;
            if (chosen != theme_selection()) {
                set_theme_selection(chosen);
                if (save_values()) {
                    theme::apply_window_theme(window_, true, true);
                } else {
                    // The write was rejected; put the row back on the stored
                    // mode so the chips never disagree with the config.
                    const auto stored = theme::mode == theme::Mode::light ? 1
                                        : theme::mode == theme::Mode::dark ? 2 : 0;
                    set_theme_selection(stored);
                }
            }
            return 0;
        }
        if (id == IDCANCEL) { close(); return 0; }
        if (id == id_text_input && notification == EN_CHANGE) {
            if (!syncing_text_ && text_callbacks_.input_changed) {
                text_callbacks_.input_changed(text_of(text_input_));
            }
            return 0;
        }
        // Command ids are shared between pages, so the branch handlers below
        // key off the control handles whenever an id could belong to two
        // controls.
        if (source == text_source_ && notification == CBN_SELCHANGE) {
            if (!syncing_text_ && text_callbacks_.source_changed) {
                text_callbacks_.source_changed(text_source_at(text_source_));
            }
            InvalidateRect(text_source_, nullptr, FALSE);
            return 0;
        }
        if (id == id_text_swap && notification == BN_CLICKED) {
            if (text_session_) {
                const std::wstring swap_source = text_session_->source();
                std::wstring swap_target = text_session_->target();
                if (swap_target.empty() && !text_session_->input().empty()) {
                    swap_target = text_session_->effective_target();
                }
                if (!swap_source.empty() && !swap_target.empty() &&
                    swap_source != swap_target) {
                    if (text_callbacks_.source_changed) {
                        text_callbacks_.source_changed(swap_target);
                    }
                    if (text_callbacks_.target_changed) {
                        text_callbacks_.target_changed(swap_source);
                    }
                    // The typed text belongs to the previous direction; drop it
                    // so the next translation starts from the new pair.
                    if (text_callbacks_.clear) text_callbacks_.clear();
                }
            }
            return 0;
        }
        if (source == text_target_ && notification == CBN_SELCHANGE) {
            if (!syncing_text_ && text_callbacks_.target_changed) {
                text_callbacks_.target_changed(text_target_at(text_target_));
            }
            InvalidateRect(text_target_, nullptr, FALSE);
            return 0;
        }
        if (id == id_text_clear && notification == BN_CLICKED) {
            if (text_callbacks_.clear) text_callbacks_.clear();
            if (text_input_) SetFocus(text_input_);
            return 0;
        }
        if (id == id_text_copy && notification == BN_CLICKED) {
            if (text_callbacks_.copy) text_callbacks_.copy();
            return 0;
        }
        if (id == id_text_capture && notification == BN_CLICKED) {
            if (capture_callback_) capture_callback_();
            return 0;
        }
        if (!loading_ && notification == BN_CLICKED && is_checkbox(source)) {
            set_checkbox(source, !checkbox_checked(source));
            save_values();
            layout_controls();
            RedrawWindow(window_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            return 0;
        }
        if ((id == id_class_traditional || id == id_class_ai) &&
            notification == BN_CLICKED && !loading_) {
            set_translation_class(id == id_class_ai);
            return 0;
        }
        if (id == id_ai_add && notification == BN_CLICKED && !loading_) {
            edit_ai_profile(std::nullopt);
            return 0;
        }
        if (source == provider_ && notification == CBN_SELCHANGE && !loading_) {
            ScopedRedrawPause redraw_pause(window_);
            save_provider_fields();
            // The provider combo lists only the traditional entries, so walking
            // the catalog with the same skip rule keeps the mapping correct
            // even if the AI row ever moves away from the end of the table.
            const int chosen = selected_index(provider_);
            int seen = 0;
            loaded_provider_ = providers.front().id;
            for (const auto& provider : providers) {
                if (provider.kind == ProviderInfo::Kind::ai) continue;
                if (seen == chosen) {
                    loaded_provider_ = provider.id;
                    break;
                }
                ++seen;
            }
            config_.set_string(L"translator.provider", loaded_provider_);
            refresh_provider_fields();
            save_values();
            layout_controls();
            RedrawWindow(window_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            return 0;
        }
        if (id == id_test && notification == BN_CLICKED) {
            test_success_ = false;
            start_connection_test();
            InvalidateRect(test_status_, nullptr, TRUE);
            return 0;
        }
        if (id == id_check_updates && notification == BN_CLICKED) {
            if (!save_values()) return 0;
            flush_config_save();
            // The update used to open its own dialog; progress is reported
            // inline next to the button so the settings window stays put.
            set_update_status(L"检查中...");
            set_update_check_enabled(false);
            if (update_check_callback_) update_check_callback_();
            return 0;
        }
        if (id == id_diagnostics && notification == BN_CLICKED) {
            // The button now opens diagnostics; the id keeps the slot stable.
            open_diagnostics_folder();
            return 0;
        }
        // A list-box row notification carries the same number as CBN_SELCHANGE,
        // and the rows already saved themselves when the click was handled, so
        // the endpoint list must not fall into this generic save.
        if (!loading_ && source != ai_list_ &&
            (notification == CBN_SELCHANGE || notification == BN_CLICKED ||
             notification == EN_KILLFOCUS)) {
            save_values();
        }
        break;
    }
    case WM_TIMER:
        if (static_cast<UINT_PTR>(wparam) == profiles_save_timer) {
            // Legacy path: refresh the fields the list pages derive, then the
            // shared config debounce persists whatever changed.
            KillTimer(window_, profiles_save_timer);
            flush_profiles_save();
            return 0;
        }
        if (static_cast<UINT_PTR>(wparam) == config_save_timer) {
            // Kill first: the flush can raise a modal error box, and the
            // timer must not fire again while that box is up.
            KillTimer(window_, config_save_timer);
            flush_config_save();
            return 0;
        }
        if (static_cast<UINT_PTR>(wparam) == delete_confirm_timer) {
            reset_delete_confirmation();
            return 0;
        }
        break;
    case test_completed_message: {
        std::unique_ptr<std::pair<bool, std::wstring>> result(
            reinterpret_cast<std::pair<bool, std::wstring>*>(lparam));
        if (result) finish_connection_test(result->first, std::move(result->second));
        return 0;
    }
    case WM_DRAWITEM:
        if (const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam)) {
            return draw_item(*item);
        }
        return FALSE;
    case WM_MEASUREITEM: {
        // The endpoint rows are owner-drawn with a fixed height, so the list box
        // is sized from the same metric the rows are painted and hit-tested with.
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
        if (measure && measure->CtlType == ODT_LISTBOX &&
            measure->CtlID == id_ai_list) {
            measure->itemHeight =
                static_cast<UINT>(px(settings_ai_list::logical_row_height(), dpi_));
            return TRUE;
        }
        break;
    }
    case WM_CTLCOLOREDIT: {
        const auto dc = reinterpret_cast<HDC>(wparam);
        const auto control = reinterpret_cast<HWND>(lparam);
        SetTextColor(dc, color_text);
        // The text translation editors live inside the shared card, so they
        // blend into the card surface instead of the usual input well.
        SetBkColor(dc, control == text_input_ ? color_card : color_input);
        SetBkMode(dc, OPAQUE);
        return reinterpret_cast<LRESULT>(control == text_input_ ? card_background_
                                                               : input_background_);
    }
    case WM_CTLCOLORLISTBOX: {
        const auto dc = reinterpret_cast<HDC>(wparam);
        if (reinterpret_cast<HWND>(lparam) == ai_list_) {
            // Endpoint rows paint themselves on the card surface, so anything
            // the rows do not cover has to blend into that card rather than the
            // input well a normal list box would use.
            SetTextColor(dc, color_text);
            SetBkColor(dc, color_card);
            SetBkMode(dc, OPAQUE);
            return reinterpret_cast<LRESULT>(card_background_);
        }
        SetTextColor(dc, color_text);
        SetBkColor(dc, color_input);
        SetBkMode(dc, OPAQUE);
        return reinterpret_cast<LRESULT>(input_background_);
    }
    case WM_CTLCOLORSTATIC: {
        const auto dc = reinterpret_cast<HDC>(wparam);
        const auto control = reinterpret_cast<HWND>(lparam);
        if (control == text_output_) {
            SetTextColor(dc, color_text);
            SetBkColor(dc, color_card);
            SetBkMode(dc, OPAQUE);
            return reinterpret_cast<LRESULT>(card_background_);
        }
        if (control == ocr_languages_ || control == capture_hotkey_ ||
            control == toggle_hotkey_ || control == text_hotkey_) {
            SetTextColor(dc, IsWindowEnabled(control) ? color_text_dim : color_text_faint);
            SetBkColor(dc, color_input);
            SetBkMode(dc, OPAQUE);
            return reinterpret_cast<LRESULT>(input_background_);
        }
        SetBkMode(dc, TRANSPARENT);
        if (control == status_) {
            SetTextColor(dc, status_error_ ? color_bad : color_text);
            return reinterpret_cast<LRESULT>(background_);
        }
        if (control == test_status_) {
            SetTextColor(dc, test_pending_ ? color_text_dim
                              : test_success_ ? color_text : color_bad);
            return reinterpret_cast<LRESULT>(card_background_);
        }
        if (control == update_status_) {
            SetTextColor(dc, update_status_error_ ? color_bad : color_text_dim);
            return reinterpret_cast<LRESULT>(card_background_);
        }
        if (control == version_label_) {
            // The version line sits below the card, on the page background.
            SetTextColor(dc, color_text_faint);
            return reinterpret_cast<LRESULT>(background_);
        }
        const auto control_font = reinterpret_cast<HFONT>(
            SendMessageW(control, WM_GETFONT, 0, 0));
        SetTextColor(dc, control_font == small_font_ ? color_text_dim : color_text);
        return reinterpret_cast<LRESULT>(card_background_);
    }
    case WM_PAINT: paint(); return 0;
    case WM_ERASEBKGND: return 1;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) { close(); return 0; }
        break;
    case WM_CLOSE: close(); return 0;
    case WM_NCDESTROY: {
        const HWND destroyed = window_;
        finished_ = true;
        KillTimer(destroyed, delete_confirm_timer);
        KillTimer(destroyed, profiles_save_timer);
        KillTimer(destroyed, config_save_timer);
        profiles_save_pending_ = false;
        config_save_pending_ = false;
        delete_deadline_ = 0;
        pending_delete_.clear();
        ai_list_tracking_ = false;
        ai_list_hover_ = -1;
        ai_hover_button_ = settings_ai_list::Button::none;
        if (test_thread_.joinable()) {
            test_thread_.request_stop();
            test_thread_.join();
        }
        discard_connection_test_results(destroyed);
        SetWindowLongPtrW(destroyed, GWLP_USERDATA, 0);
        window_ = nullptr;
        return DefWindowProcW(destroyed, message, wparam, lparam);
    }
    default: break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace minitrans
