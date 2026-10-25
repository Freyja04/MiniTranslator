#include "settings_window.hpp"
#include "settings_window_internal.hpp"

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
#include <cstdlib>
#include <filesystem>
#include <memory>

namespace minitrans {

using namespace settings_internal;

SettingsWindow::SettingsWindow(HINSTANCE instance, ConfigStore& config)
    : instance_(instance), config_(config) {}

SettingsWindow::~SettingsWindow() {
    // A debounced endpoint save may still be pending when the window is torn
    // down without close(). The list already lives in the in-memory config, so
    // only the disk write is owed here; reading the controls would touch
    // handles that are gone.
    if (profiles_save_pending_) {
        profiles_save_pending_ = false;
        config_.save();
    }
    if (test_thread_.joinable()) {
        test_thread_.request_stop();
        test_thread_.join();
    }
    discard_connection_test_results();
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    if (font_) DeleteObject(font_);
    if (small_font_) DeleteObject(small_font_);
    if (icon_font_) DeleteObject(icon_font_);
    if (action_icon_font_) DeleteObject(action_icon_font_);
    if (background_) DeleteObject(background_);
    if (sidebar_background_) DeleteObject(sidebar_background_);
    if (card_background_) DeleteObject(card_background_);
    if (input_background_) DeleteObject(input_background_);
}

bool SettingsWindow::preprocess_message(MSG& message) {
    const HWND dialog = window_;
    if (!dialog || !IsWindow(dialog) || !IsWindowVisible(dialog)) return false;
    if (current_page_ == 4 && message.message == WM_KEYDOWN &&
        message.wParam == VK_RETURN && (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (text_callbacks_.translate_now) text_callbacks_.translate_now();
        return true;
    }
    return IsDialogMessageW(dialog, &message) != FALSE;
}

void SettingsWindow::show(HWND owner) {
    if (window_) {
        ShowWindow(window_, SW_RESTORE);
        SetForegroundWindow(window_);
        return;
    }
    finished_ = false;
    test_pending_ = false;
    create_window(owner);
    const HWND window = window_;
    if (!window) throw AppError("settings window creation returned no window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
}

void SettingsWindow::show_translation(HWND owner) {
    show(owner);
    if (!window_) return;
    show_page(1);
    layout_controls();
    InvalidateRect(window_, nullptr, TRUE);
}

void SettingsWindow::attach_text_translation(
        TextTranslationSession& session, TextTranslationCallbacks callbacks) {
    text_session_ = &session;
    text_callbacks_ = std::move(callbacks);
    refresh_text_translation();
}

void SettingsWindow::detach_text_translation() noexcept {
    text_callbacks_ = {};
    text_session_ = nullptr;
}

void SettingsWindow::show_text_translation(HWND owner) {
    show(owner);
    if (!window_) return;
    show_page(4);
    refresh_text_translation();
    if (text_input_) SetFocus(text_input_);
}

void SettingsWindow::hide_for_capture() {
    // The region overlay needs a clean screen: hide this window before the
    // capture starts and restore it once the overlay closes.
    if (window_ && IsWindowVisible(window_)) ShowWindow(window_, SW_HIDE);
}

void SettingsWindow::show_after_capture() {
    if (window_ && IsWindow(window_) && !IsWindowVisible(window_)) {
        ShowWindow(window_, SW_SHOWNA);
        SetForegroundWindow(window_);
    }
}

void SettingsWindow::refresh_text_translation() {
    if (!window_ || !text_session_ || !text_input_ || !text_output_ ||
        !text_source_ || !text_target_) {
        return;
    }
    syncing_text_ = true;
    if (text_of(text_input_) != text_session_->input()) {
        SetWindowTextW(text_input_, text_session_->input().c_str());
    }
    if (text_of(text_output_) != text_session_->output()) {
        SetWindowTextW(text_output_, text_session_->output().c_str());
    }
    const int expected_source = text_source_index(text_session_->source());
    if (expected_source >= 0 && selected_index(text_source_) != expected_source) {
        select_combo(text_source_, expected_source);
    }
    const int expected_target = text_target_index(text_session_->target());
    if (selected_index(text_target_) != expected_target) {
        select_combo(text_target_, expected_target);
    }
    syncing_text_ = false;
    EnableWindow(text_copy_, !text_session_->output().empty());
    EnableWindow(text_clear_, !text_session_->input().empty());
    InvalidateRect(text_target_, nullptr, FALSE);
    if (current_page_ == 4) InvalidateRect(window_, nullptr, FALSE);
}

void SettingsWindow::create_window(HWND owner) {
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = &SettingsWindow::window_proc;
    description.style = 0;
    description.hInstance = instance_;
    description.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    description.hIconSm = description.hIcon;
    description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    description.lpszClassName = class_name;
    if (!RegisterClassExW(&description) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw_last_error("register settings window");
    }
    RECT owner_rect{};
    const bool usable_owner = owner && IsWindowVisible(owner) && GetWindowRect(owner, &owner_rect) &&
        owner_rect.right - owner_rect.left >= 100 && owner_rect.bottom - owner_rect.top >= 100;
    HMONITOR monitor{};
    if (usable_owner) {
        monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    } else {
        POINT cursor{};
        GetCursorPos(&cursor);
        monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{sizeof(info)};
        if (GetMonitorInfoW(monitor, &info)) {
            owner_rect = info.rcWork;
        } else {
            owner_rect = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        }
    }
    UINT monitor_dpi_x = 96;
    UINT monitor_dpi_y = 96;
    if (!monitor || FAILED(GetDpiForMonitor(
            monitor, MDT_EFFECTIVE_DPI, &monitor_dpi_x, &monitor_dpi_y))) {
        monitor_dpi_x = GetDpiForSystem();
    }
    dpi_ = usable_owner ? static_cast<int>(GetDpiForWindow(owner))
                        : static_cast<int>(monitor_dpi_x);
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                        WS_THICKFRAME | WS_MINIMIZEBOX | WS_CLIPCHILDREN;
    // Page changes show, hide, and move several native child controls. Compose
    // the complete descendant tree before presenting it so owner-drawn buttons
    // and editable combo boxes never expose their default light erase frame.
    constexpr DWORD extended_style = WS_EX_APPWINDOW | WS_EX_COMPOSITED;
    RECT desired{0, 0, px(790, dpi_), px(logical_window_height, dpi_)};
    AdjustWindowRectExForDpi(&desired, style, FALSE, extended_style,
                             static_cast<UINT>(dpi_));
    const int width = desired.right - desired.left;
    const int height = desired.bottom - desired.top;
    const int x = owner_rect.left + std::max(0L, (owner_rect.right - owner_rect.left - width) / 2);
    const int y = owner_rect.top + std::max(0L, (owner_rect.bottom - owner_rect.top - height) / 2);
    window_error_.clear();
    window_ = CreateWindowExW(
        extended_style, class_name, MINITRANS_DISPLAY_NAME,
        style,
        x, y, width, height, nullptr, nullptr, instance_, this);
    if (!window_) {
        if (!window_error_.empty()) throw AppError(window_error_);
        throw_last_error("create settings window");
    }
    theme::apply_window_theme(window_, true, true);
}

HWND SettingsWindow::add_control(int page, DWORD ex_style, const wchar_t* type,
                                 const wchar_t* text, DWORD style, int identifier) {
    if (_wcsicmp(type, L"EDIT") == 0) {
        // A native EDIT can paint its classic square non-client frame once
        // before a later style change takes effect. Create it borderless from
        // the outset; the parent paints the shared antialiased rounded frame.
        style &= ~WS_BORDER;
        ex_style &= ~(WS_EX_CLIENTEDGE | WS_EX_STATICEDGE);
    }
    HWND control = CreateWindowExW(
        ex_style, type, text, WS_CHILD | WS_VISIBLE | style,
        0, 0, 1, 1, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)), instance_, nullptr);
    if (!control) throw_last_error("create settings control");
    set_font(control, font_);
    apply_control_theme(control);
    if (page >= 0) page_controls_[static_cast<std::size_t>(page)].push_back(control);
    return control;
}

HWND SettingsWindow::add_label(int page, const wchar_t* text) {
    return add_control(page, 0, L"STATIC", text, SS_LEFT, 0);
}

void SettingsWindow::create_theme_resources() {
    for (auto** font : {&font_, &small_font_, &icon_font_,
                        &action_icon_font_}) {
        if (*font) DeleteObject(*font);
        *font = nullptr;
    }
    for (auto** brush : {&background_, &sidebar_background_, &card_background_,
                         &input_background_}) {
        if (*brush) DeleteObject(*brush);
        *brush = nullptr;
    }

    const auto make_font = [&](int pixels, int weight) {
        return CreateFontW(-px(pixels, dpi_), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH, theme::font_family.c_str());
    };
    font_ = make_font(theme::font_size_body, FW_NORMAL);
    small_font_ = make_font(theme::font_size_small, FW_NORMAL);
    icon_font_ = create_icon_font(dpi_);
    action_icon_font_ = create_icon_font(dpi_, 12);
    background_ = CreateSolidBrush(color_page);
    sidebar_background_ = CreateSolidBrush(color_sidebar);
    card_background_ = CreateSolidBrush(color_card);
    input_background_ = CreateSolidBrush(color_input);
}

void SettingsWindow::refresh_appearance() {
    if (!window_) return;
    create_theme_resources();
    for (const auto button : navigation_) set_font(button, font_);
    set_font(status_, small_font_);
    for (auto& controls : page_controls_) {
        for (const auto control : controls) {
            set_font(control, font_);
            apply_control_theme(control);
        }
    }
    set_font(test_status_, small_font_);
    set_font(provider_hint_, small_font_);
    theme::apply_window_theme(window_, true, true);
    layout_controls();
    RedrawWindow(window_, nullptr, nullptr,
                 RDW_FRAME | RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE |
                     RDW_UPDATENOW);
}

void SettingsWindow::apply_control_theme(HWND control) {
    if (!control) return;
    wchar_t class_name_buffer[32]{};
    GetClassNameW(control, class_name_buffer, static_cast<int>(std::size(class_name_buffer)));
    if (_wcsicmp(class_name_buffer, L"Edit") == 0) {
        // The Explorer theme paints its own square edit frame even without
        // WS_BORDER.  The settings window supplies the shared rounded field.
        SetWindowTheme(control, L"", L"");
        const auto style = GetWindowLongPtrW(control, GWL_STYLE);
        SetWindowLongPtrW(control, GWL_STYLE,
                          style & ~static_cast<LONG_PTR>(WS_BORDER));
        const auto extended_style = GetWindowLongPtrW(control, GWL_EXSTYLE);
        SetWindowLongPtrW(control, GWL_EXSTYLE,
                          extended_style & ~static_cast<LONG_PTR>(WS_EX_CLIENTEDGE |
                                                                  WS_EX_STATICEDGE));
        SetWindowPos(control, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                          SWP_FRAMECHANGED);
        const int text_margin =
            GetPropW(control, L"MiniTranslator.Hotkey") ? 12 : 9;
        SendMessageW(control, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(px(text_margin, dpi_), px(text_margin, dpi_)));
        SetWindowSubclass(control, &SettingsWindow::edit_proc, edit_subclass_id,
                          reinterpret_cast<DWORD_PTR>(this));
    } else if (_wcsicmp(class_name_buffer, L"ComboBox") == 0) {
        theme::apply_control_theme(control);
        COMBOBOXINFO info{sizeof(info)};
        if (GetComboBoxInfo(control, &info) && info.hwndItem && info.hwndItem != control) {
            theme::apply_control_theme(info.hwndItem);
            set_font(info.hwndItem, font_);
            SendMessageW(info.hwndItem, EM_SETMARGINS,
                         EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELPARAM(px(9, dpi_), px(9, dpi_)));
            SetWindowSubclass(info.hwndItem, &SettingsWindow::combo_edit_proc,
                              combo_edit_subclass_id,
                              reinterpret_cast<DWORD_PTR>(this));
        }
        style_combo_popup(control, dpi_, font_);
        SetWindowSubclass(control, &SettingsWindow::combo_proc, combo_subclass_id,
                          reinterpret_cast<DWORD_PTR>(this));
        align_combo_edit(control, dpi_);
    } else {
        theme::apply_control_theme(control);
    }
}

LRESULT CALLBACK SettingsWindow::edit_proc(HWND control, UINT message,
                                            WPARAM wparam, LPARAM lparam,
                                            UINT_PTR subclass_id, DWORD_PTR reference) {
    auto* self = reinterpret_cast<SettingsWindow*>(reference);
    // The translation output is display-only. Focus attempts are forwarded to
    // the source editor so the output never grows a caret or selection frame.
    if (self && control == self->text_output_ &&
        (message == WM_SETFOCUS || message == WM_LBUTTONDOWN ||
         message == WM_LBUTTONDBLCLK)) {
        if (self->text_input_ && IsWindow(self->text_input_)) {
            SetFocus(self->text_input_);
        }
        return 0;
    }
    const auto update_region = [&] {
        if (!self) return;
        RECT rect{};
        GetClientRect(control, &rect);
        if (rect.right <= rect.left || rect.bottom <= rect.top) return;
        const int inset = std::max(1, px(logical_edit_inset_x, self->dpi_));
        const int inner_radius = std::max(
            1, px(logical_edit_corner_radius, self->dpi_) - inset);
        const int diameter = inner_radius * 2;
        HRGN region = CreateRoundRectRgn(
            0, 0, rect.right + 1, rect.bottom + 1, diameter, diameter);
        if (region && !SetWindowRgn(control, region, TRUE)) DeleteObject(region);
    };
    switch (message) {
    case WM_NCPAINT:
        // The settings window owns the edit frame. Letting the EDIT class
        // repaint its non-client area would put a square system border over it.
        return 0;
    case WM_SIZE: {
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        update_region();
        return result;
    }
    case WM_IME_STARTCOMPOSITION:
        if (self && control == self->text_input_ &&
            self->text_callbacks_.composition_changed) {
            self->text_callbacks_.composition_changed(true);
        }
        break;
    case WM_IME_ENDCOMPOSITION:
        if (self && control == self->text_input_ &&
            self->text_callbacks_.composition_changed) {
            self->text_callbacks_.composition_changed(false);
        }
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        if (self && self->window_) InvalidateRect(self->window_, nullptr, FALSE);
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, &SettingsWindow::edit_proc, subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT CALLBACK SettingsWindow::combo_proc(HWND control, UINT message,
                                            WPARAM wparam, LPARAM lparam,
                                            UINT_PTR subclass_id, DWORD_PTR reference) {
    auto* self = reinterpret_cast<SettingsWindow*>(reference);
    switch (message) {
    case WM_SIZE: {
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        if (self) align_combo_edit(control, self->dpi_);
        return result;
    }
    case WM_PAINT: {
        PAINTSTRUCT state{};
        HDC dc = BeginPaint(control, &state);
        if (dc && self) {
            try {
                self->draw_combo(control, dc);
            } catch (...) {
                RECT rect{};
                GetClientRect(control, &rect);
                FillRect(dc, &rect, self->input_background_);
            }
        }
        EndPaint(control, &state);
        return 0;
    }
    case WM_PRINTCLIENT:
        if (self) {
            try {
                self->draw_combo(control, reinterpret_cast<HDC>(wparam));
            } catch (...) {
                return 0;
            }
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        if (message == WM_SETFOCUS) {
            if (self) align_combo_edit(control, self->dpi_);
            clear_combo_edit_selection(control);
        }
        InvalidateRect(control, nullptr, FALSE);
        return result;
    }
    case WM_LBUTTONDOWN: {
        set_combo_edit_redraw(control, false);
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        if (self) align_combo_edit(control, self->dpi_);
        clear_combo_edit_selection(control);
        if (!SendMessageW(control, CB_GETDROPPEDSTATE, 0, 0)) {
            set_combo_edit_redraw(control, true);
        }
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, &SettingsWindow::combo_proc, subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT CALLBACK SettingsWindow::combo_edit_proc(HWND control, UINT message,
                                                 WPARAM wparam, LPARAM lparam,
                                                 UINT_PTR subclass_id,
                                                 DWORD_PTR reference) {
    auto* self = reinterpret_cast<SettingsWindow*>(reference);
    switch (message) {
    case WM_SETFOCUS:
    case WM_LBUTTONDOWN:
    case WM_WINDOWPOSCHANGED: {
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        const HWND combo = GetParent(control);
        if (self && combo) align_combo_edit(combo, self->dpi_);
        if (message == WM_SETFOCUS) {
            clear_combo_edit_selection(combo);
        }
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, &SettingsWindow::combo_edit_proc,
                             subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT CALLBACK SettingsWindow::hotkey_proc(HWND control, UINT message,
                                             WPARAM wparam, LPARAM lparam,
                                             UINT_PTR subclass_id, DWORD_PTR reference) {
    auto* self = reinterpret_cast<SettingsWindow*>(reference);
    try {
    switch (message) {
    case WM_GETDLGCODE:
        return DLGC_WANTALLKEYS;
    case WM_SETFOCUS: {
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        SendMessageW(control, EM_SETSEL, 0, -1);
        return result;
    }
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN: {
        const UINT key = static_cast<UINT>(wparam);
        if (is_modifier_key(key)) return 0;
        if (!self) return 0;
        if (key == VK_ESCAPE) {
            SetFocus(self->navigation_[navigation_index_for_page(self->current_page_)]);
            return 0;
        }
        const auto name = hotkey_key_name(key);
        if (name.empty()) {
            MessageBeep(MB_ICONWARNING);
            self->set_status(L"这个按键不能用作快捷键", true);
            return 0;
        }
        std::wstring value;
        const auto append = [&](std::wstring_view part) {
            if (!value.empty()) value += L'+';
            value += part;
        };
        if (GetKeyState(VK_CONTROL) < 0) append(L"Ctrl");
        if (GetKeyState(VK_MENU) < 0) append(L"Alt");
        if (GetKeyState(VK_SHIFT) < 0) append(L"Shift");
        if (GetKeyState(VK_LWIN) < 0 || GetKeyState(VK_RWIN) < 0) append(L"Win");
        const bool function_key = key >= VK_F1 && key <= VK_F24;
        if (value.empty() && !function_key) {
            MessageBeep(MB_ICONWARNING);
            self->set_status(L"除 F1 到 F24 外，快捷键必须带 Ctrl、Alt、Shift 或 Win", true);
            return 0;
        }
        append(name);
        SetWindowTextW(control, value.c_str());
        self->save_values();
        SetFocus(self->navigation_[navigation_index_for_page(self->current_page_)]);
        return 0;
    }
    case WM_CHAR:
    case WM_SYSCHAR:
    case WM_PASTE:
    case WM_CUT:
    case WM_CLEAR:
        return 0;
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, &SettingsWindow::hotkey_proc, subclass_id);
        break;
    default:
        break;
    }
    } catch (...) {
        if (message == WM_KEYDOWN || message == WM_SYSKEYDOWN) {
            MessageBeep(MB_ICONWARNING);
            return 0;
        }
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT CALLBACK SettingsWindow::list_proc(HWND control, UINT message,
                                           WPARAM wparam, LPARAM lparam,
                                           UINT_PTR subclass_id,
                                           DWORD_PTR reference) {
    auto* self = reinterpret_cast<SettingsWindow*>(reference);
    switch (message) {
    case WM_GETDLGCODE:
        // The rows are buttons, not a dialog selection, so IsDialogMessage
        // must not treat the list as an editable control and hammer it with
        // focus/dialog traffic: a focused owner-drawn LBS_NOSEL row enters a
        // focus-repaint loop that flickers for as long as the window is open.
        return DLGC_WANTARROWS;
    case WM_SETCURSOR:
        // The row buttons paint their own hover, so the cursor has to be set
        // here and the default handler skipped; a returned TRUE keeps the class
        // cursor from overwriting the hand pointer on the next mouse move.
        if (self && LOWORD(lparam) == HTCLIENT) {
            POINT cursor{};
            GetCursorPos(&cursor);
            ScreenToClient(control, &cursor);
            self->update_ai_hover(control, cursor);
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
        if (self) {
            self->update_ai_hover(control, POINT{GET_X_LPARAM(lparam),
                                                 GET_Y_LPARAM(lparam)});
        }
        break;
    case WM_MOUSELEAVE:
        if (self) {
            self->ai_list_tracking_ = false;
            self->clear_ai_hover(control);
        }
        break;
    case WM_VSCROLL:
    case WM_MOUSEWHEEL: {
        // Scrolling moves the rows under a hover index that no longer matches
        // the pointer, so the highlight is dropped and the next mouse position
        // picks the row that ended up under the cursor.
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        if (self) self->clear_ai_hover(control);
        return result;
    }
    case WM_LBUTTONUP: {
        // The rows are button targets, not list selections, so the click is
        // handled on release the way a button is: the pointer can still move
        // away from the target before the action happens.
        if (self) {
            self->on_ai_list_click(POINT{GET_X_LPARAM(lparam),
                                         GET_Y_LPARAM(lparam)});
        }
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        return 0;
    case WM_NCDESTROY:
        if (self) {
            self->ai_list_tracking_ = false;
            self->ai_list_hover_ = -1;
            self->ai_hover_button_ = settings_ai_list::Button::none;
        }
        RemoveWindowSubclass(control, &SettingsWindow::list_proc, subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

void SettingsWindow::update_ai_hover(HWND list, POINT list_point) {
    if (!list || current_page_ != 1 || !large_model()) return;
    const auto [row, button] =
        settings_ai_list::hit_test(list, list_point, dpi_, shown_rows());
    if (!ai_list_tracking_) {
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, list, 0};
        ai_list_tracking_ = TrackMouseEvent(&track) != FALSE;
    }
    if (row == ai_list_hover_ && button == ai_hover_button_) return;
    const int previous = ai_list_hover_;
    ai_list_hover_ = row;
    ai_hover_button_ = button;
    // Only the rows whose highlight changed are redrawn; the list box owns the
    // row surface, so the parent card is untouched.
    settings_ai_list::invalidate_row(list, previous);
    settings_ai_list::invalidate_row(list, row);
    SetCursor(LoadCursorW(nullptr, button == settings_ai_list::Button::none
                                        ? IDC_ARROW
                                        : IDC_HAND));
}

void SettingsWindow::clear_ai_hover(HWND list) noexcept {
    if (ai_list_hover_ < 0 && ai_hover_button_ == settings_ai_list::Button::none) {
        return;
    }
    const int previous = ai_list_hover_;
    ai_list_hover_ = -1;
    ai_hover_button_ = settings_ai_list::Button::none;
    if (list && IsWindow(list)) settings_ai_list::invalidate_row(list, previous);
}

void SettingsWindow::on_ai_list_click(POINT list_point) {
    const auto& rows = shown_rows();
    const auto [row, button] =
        settings_ai_list::hit_test(ai_list_, list_point, dpi_, rows);
    // The rows are buttons, never a selection. The default list box gives
    // itself keyboard focus on mouse-down, and a focused owner-drawn
    // LBS_NOSEL row degenerates into a focus-repaint loop that flickers the
    // whole row for as long as the window stays open. Hand the focus back to
    // the page's sidebar entry, the same place show_page() puts it; the
    // sidebar keeps keyboard navigation and nothing else changes hands.
    if (GetFocus() == ai_list_) {
        const HWND navigation =
            navigation_[navigation_index_for_page(current_page_)];
        if (navigation) SetFocus(navigation);
    }
    if (row < 0 || button == settings_ai_list::Button::none ||
        static_cast<std::size_t>(row) >= rows.size()) {
        reset_delete_confirmation();
        return;
    }
    const std::size_t index = static_cast<std::size_t>(row);
    if (!large_model()) {
        // The traditional rows only toggle (DeepL opens its key editor first).
        if (button == settings_ai_list::Button::enable ||
            button == settings_ai_list::Button::edit) {
            reset_delete_confirmation();
            select_traditional(index);
        }
        return;
    }
    if (button == settings_ai_list::Button::enable) {
        reset_delete_confirmation();
        select_ai_profile(index);
        return;
    }
    if (button == settings_ai_list::Button::edit) {
        reset_delete_confirmation();
        edit_ai_profile(index);
        return;
    }
    // Deleting an endpoint is irreversible, so the button asks for a second
    // click before it acts; the caption changes to 「确认?」 until the timer
    // resets it.
    const std::wstring name = ai_rows_[index].name;
    if (pending_delete_ == name &&
        GetTickCount64() < delete_deadline_) {
        remove_ai_profile(index);
        return;
    }
    reset_delete_confirmation();
    pending_delete_ = name;
    delete_deadline_ = GetTickCount64() + delete_confirm_timeout_ms;
    SetTimer(window_, delete_confirm_timer, delete_confirm_timeout_ms, nullptr);
    settings_ai_list::invalidate_row(ai_list_, static_cast<int>(index));
}

void SettingsWindow::set_translation_class(bool large_model) {
    ScopedRedrawPause redraw_pause(window_);
    save_provider_fields();
    reset_delete_confirmation();
    clear_ai_hover(ai_list_);
    loaded_provider_ = large_model ? std::wstring(L"ai")
                                  : std::wstring(settings_provider::id_at(
                                        std::max(0, selected_index(provider_))));
    config_.set_string(L"translator.provider", loaded_provider_);
    populate_ai_list();
    refresh_provider_fields();
    save_values();
    layout_controls();
    RedrawWindow(window_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void SettingsWindow::draw_combo(HWND control, HDC dc) {
    if (!dc) return;
    RECT rect{};
    GetClientRect(control, &rect);
    std::wstring value = text_of(control);
    if (text_session_) {
        if (control == text_target_) {
            value = text_target_selector(*text_session_);
        } else if (control == text_source_) {
            value = target_display_name(text_session_->source());
        }
    }
    ui_controls::FieldStyle style;
    style.background = theme::color_card;
    style.fill = color_input;
    style.border = color_line;
    style.focus_border = color_line;
    style.text = color_text;
    style.disabled_text = color_text_faint;
    style.chevron = color_text_dim;
    style.font = font_;
    style.dpi = dpi_;
    style.enabled = IsWindowEnabled(control) != FALSE;
    style.focused = GetFocus() == control || IsChild(control, GetFocus());
    ui_controls::draw_field(dc, rect, value, style,
                            &settings_internal::fill_round_rect);
}

bool SettingsWindow::is_checkbox(HWND control) const noexcept {
    return control && (control == ocr_upscale_ || control == auto_copy_ ||
                       control == autostart_);
}

bool SettingsWindow::checkbox_checked(HWND control) const noexcept {
    return GetPropW(control, L"MiniTranslator.Checked") != nullptr;
}

void SettingsWindow::set_checkbox(HWND control, bool checked) {
    if (!control) return;
    if (checked) SetPropW(control, L"MiniTranslator.Checked", reinterpret_cast<HANDLE>(1));
    else RemovePropW(control, L"MiniTranslator.Checked");
    InvalidateRect(control, nullptr, FALSE);
}

int SettingsWindow::theme_selection() const noexcept {
    if (theme_mode_ && GetPropW(theme_mode_, L"MiniTranslator.SegmentOn")) return 0;
    if (theme_segment_[0] && GetPropW(theme_segment_[0], L"MiniTranslator.SegmentOn")) {
        return 1;
    }
    return 2;
}

void SettingsWindow::set_theme_selection(int index) {
    const std::array<HWND, 3> chips{
        theme_mode_, theme_segment_[0], theme_segment_[1],
    };
    for (std::size_t chip = 0; chip < chips.size(); ++chip) {
        if (!chips[chip]) continue;
        if (static_cast<int>(chip) == index) {
            SetPropW(chips[chip], L"MiniTranslator.SegmentOn",
                     reinterpret_cast<HANDLE>(1));
        } else {
            RemovePropW(chips[chip], L"MiniTranslator.SegmentOn");
        }
        InvalidateRect(chips[chip], nullptr, FALSE);
    }
}

void SettingsWindow::show_page(int index) {
    if (!window_) return;
    current_page_ = std::clamp(
        index, 0, static_cast<int>(page_controls_.size() - 1));
    for (std::size_t page = 0; page < page_controls_.size(); ++page) {
        for (const auto control : page_controls_[page]) {
            ShowWindow(control, static_cast<int>(page) == current_page_ ? SW_SHOW : SW_HIDE);
        }
    }
    if (current_page_ == 1) refresh_provider_fields();
    if (current_page_ == 4) refresh_text_translation();
    layout_controls();
    const auto navigation_index = navigation_index_for_page(current_page_);
    if (navigation_[navigation_index]) {
        SetFocus(navigation_[navigation_index]);
    }
    for (const auto button : navigation_) InvalidateRect(button, nullptr, FALSE);
    InvalidateRect(window_, nullptr, FALSE);
}

void SettingsWindow::close() {
    // The endpoint list's debounced save still belongs to this window, so a
    // pending disk write is flushed before the controls go away.
    flush_profiles_save();
    if (!save_values()) return;
    if (text_callbacks_.composition_changed) {
        text_callbacks_.composition_changed(false);
    }
    if (test_thread_.joinable()) test_thread_.request_stop();
    finished_ = true;
    const HWND closing = window_;
    if (closing && IsWindow(closing)) DestroyWindow(closing);
}

LRESULT CALLBACK SettingsWindow::window_proc(HWND window, UINT message,
                                              WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<SettingsWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    try {
        return self ? self->handle_message(message, wparam, lparam)
                    : DefWindowProcW(window, message, wparam, lparam);
    } catch (const std::exception& error) {
        if (self) {
            try { self->window_error_ = error.what(); } catch (...) {}
        }
    } catch (...) {
        if (self) {
            try { self->window_error_ = "unknown settings window error"; } catch (...) {}
        }
    }
    return message == WM_NCCREATE ? FALSE : message == WM_CREATE ? -1 : 0;
}

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
            // The update used to open its own dialog; progress is reported
            // inline next to the button so the settings window stays put.
            set_update_status(L"检查中...");
            set_update_check_enabled(false);
            if (update_check_callback_) update_check_callback_();
            return 0;
        }
        if (id == id_restart && notification == BN_CLICKED) {
            if (save_values() && restart_callback_) {
                finished_ = true;
                ShowWindow(window_, SW_HIDE);
                restart_callback_();
            }
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
            // Kill first: save_values() can raise a modal error box, and the
            // timer must not fire again while that box is up.
            KillTimer(window_, profiles_save_timer);
            flush_profiles_save();
            return 0;
        }
        if (static_cast<UINT_PTR>(wparam) == delete_confirm_timer) {
            reset_delete_confirmation();
            return 0;
        }
        if (static_cast<UINT_PTR>(wparam) == profiles_save_timer) {
            // Kill first: save_values() can raise a modal error box, and the
            // timer must not fire again while that box is up.
            KillTimer(window_, profiles_save_timer);
            flush_profiles_save();
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
        profiles_save_pending_ = false;
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
