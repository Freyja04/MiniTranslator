#include "text_translation_window_internal.hpp"

#include "language.hpp"
#include "resource.h"
#include "theme.hpp"
#include "ui_controls.hpp"
#include "ui_icons.hpp"
#include "util.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <windowsx.h>
#include <winrt/Windows.Data.Json.h>

#include <algorithm>
#include <memory>

namespace minitrans {

LRESULT QuickTranslateWindow::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case id_input:
            if (HIWORD(wparam) == EN_CHANGE) {
                InvalidateRect(input_, nullptr, FALSE);
                on_input_changed();
            }
            if (HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS) {
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        case id_output:
            if (HIWORD(wparam) == EN_SETFOCUS || HIWORD(wparam) == EN_KILLFOCUS) {
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        case id_source:
            if (HIWORD(wparam) == CBN_DROPDOWN) {
                theme::apply_control_theme(source_combo_);
                style_target_popup(source_combo_, dpi_, false);
            }
            if (HIWORD(wparam) == CBN_SELCHANGE) on_source_changed();
            return 0;
        case id_target:
            if (HIWORD(wparam) == CBN_DROPDOWN) {
                theme::apply_control_theme(target_combo_);
                style_target_popup(target_combo_, dpi_, false);
            }
            if (HIWORD(wparam) == CBN_SELCHANGE) on_target_changed();
            return 0;
        case id_swap:
            if (HIWORD(wparam) == BN_CLICKED) swap_languages();
            return 0;
        case id_copy:
            if (HIWORD(wparam) == BN_CLICKED && callbacks_.copy) {
                callbacks_.copy();
            }
            return 0;
        case id_clear:
            if (HIWORD(wparam) == BN_CLICKED) {
                if (callbacks_.clear) callbacks_.clear();
                SetFocus(input_);
            }
            return 0;
        case id_quick_capture:
            if (HIWORD(wparam) == BN_CLICKED) {
                hide();
                if (callbacks_.capture) callbacks_.capture();
            }
            return 0;
        case id_quick_home:
            if (HIWORD(wparam) == BN_CLICKED && callbacks_.home) {
                callbacks_.home();
            }
            return 0;
        case id_quick_close:
            if (HIWORD(wparam) == BN_CLICKED) hide();
            return 0;
        default: break;
        }
        break;
    case WM_SIZE: layout_controls(); return 0;
    case WM_DPICHANGED:
        dpi_ = HIWORD(wparam);
        if (const auto* suggested = reinterpret_cast<const RECT*>(lparam)) {
            SetWindowPos(window_, HWND_TOPMOST, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOACTIVATE);
        }
        refresh_appearance();
        layout_controls();
        return 0;
    case theme::changed_message:
        refresh_appearance();
        return 0;
    case WM_NCHITTEST: {
        const LRESULT hit = DefWindowProcW(window_, message, wparam, lparam);
        if (hit != HTCLIENT) return hit;
        const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        if (point_in_window(input_, point) || point_in_window(output_, point) ||
            point_in_window(source_combo_, point) ||
            point_in_window(swap_button_, point) ||
            point_in_window(target_combo_, point) ||
            point_in_window(copy_button_, point) ||
            point_in_window(clear_button_, point) ||
            point_in_window(capture_button_, point) ||
            point_in_window(home_button_, point)) {
            return HTCLIENT;
        }
        return HTCAPTION;
    }
    case WM_DRAWITEM:
        if (lparam) return draw_item(*reinterpret_cast<const DRAWITEMSTRUCT*>(lparam));
        break;
    case WM_CTLCOLOREDIT: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, color_text);
        SetBkColor(dc, color_surface);
        return reinterpret_cast<LRESULT>(input_background_);
    }
    case WM_CTLCOLORSTATIC: {
        HDC dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, color_text);
        SetBkColor(dc, color_surface);
        return reinterpret_cast<LRESULT>(input_background_);
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(); return 0;
    case WM_CLOSE: hide(); return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        if (output_) {
            RemoveWindowSubclass(output_, &keep_editor_unfocused,
                                 output_editor_subclass_id);
        }
        window_ = nullptr;
        source_combo_ = swap_button_ = nullptr;
        target_combo_ = input_ = output_ = nullptr;
        capture_button_ = home_button_ = close_button_ = nullptr;
        copy_button_ = clear_button_ = nullptr;
        return 0;
    default: break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace minitrans
