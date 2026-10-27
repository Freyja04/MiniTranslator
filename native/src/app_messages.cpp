#include "app.hpp"

#include "resource.h"
#include "theme.hpp"
#include "tray_menu.hpp"
#include "util.hpp"

#include <windowsx.h>

#include <cwchar>

namespace minitrans {

LRESULT CALLBACK AppHost::window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<AppHost*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<AppHost*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handle_message(message, wparam, lparam)
                : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT AppHost::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    if (taskbar_created_message_ && message == taskbar_created_message_) {
        tray_.hWnd = nullptr;
        if (icon_) {
            DestroyIcon(icon_);
            icon_ = nullptr;
        }
        try {
            add_tray_icon();
        } catch (...) {
            // Explorer can broadcast TaskbarCreated before its notification area is ready.
        }
        return 0;
    }
    switch (message) {
    case initialize_message:
        if (self_test_mode_) return 0;
        try {
            add_tray_icon();
            apply_hotkeys(false);
            const bool reported_recovery = config_.recovered();
            if (reported_recovery) {
                notify(MINITRANS_DISPLAY_NAME, config_.recovery_message());
            }
            if (text_translation_on_start_) {
                open_text_translation();
            } else if (!reported_recovery) {
                notify(MINITRANS_DISPLAY_NAME,
                       L"已启动，左键打开快速翻译，右键菜单可进入设置；按 " +
                       config_.string(L"hotkey", L"Ctrl+Alt+Q") + L" 截图翻译");
            }
            // The silent update check is pure background work: give the
            // window its first frame and the user's first click a clear field
            // before it starts, instead of racing them at a fixed 1.5s.
            SetTimer(window_, update_timer, update_check_delay_ms, nullptr);
        } catch (const std::exception& error) {
            const auto message_text = std::wstring(MINITRANS_DISPLAY_NAME) +
                                      L" 无法初始化通知区域图标。\n\n" +
                                      utf8_to_wide(error.what());
            MessageBoxW(nullptr, message_text.c_str(), MINITRANS_DISPLAY_NAME,
                        MB_OK | MB_ICONERROR);
            quit();
        }
        return 0;
    case activate_message:
        if (wparam == activate_text_translation) open_text_translation();
        else toggle_result();
        return 0;
    case WM_HOTKEY:
        if (wparam == hotkey_capture) start_capture();
        else if (wparam == hotkey_toggle) toggle_result();
        else if (wparam == hotkey_text_translate) toggle_quick_translation();
        return 0;
    case WM_SETTINGCHANGE:
        if (theme::mode == theme::Mode::system && lparam &&
            wcscmp(reinterpret_cast<const wchar_t*>(lparam),
                   L"ImmersiveColorSet") == 0) {
            theme::refresh_system_mode();
            refresh_theme_windows();
        } else if (!lparam || wcscmp(reinterpret_cast<const wchar_t*>(lparam),
                                     L"WindowMetrics") == 0 ||
                   wcscmp(reinterpret_cast<const wchar_t*>(lparam),
                          L"Intl") == 0) {
            theme::refresh_system_ui_font();
            refresh_theme_windows();
        }
        return 0;
    case tray_message: {
        const UINT event = LOWORD(lparam);
        // NOTIFYICON_VERSION_4 reports a left click as NIN_SELECT; handling the
        // raw mouse messages too would toggle the panel twice in one click.
        if (event == NIN_SELECT || event == NIN_KEYSELECT) {
            if (quick_window_ && !overlay_.active()) {
                if (text_session_) text_session_->prewarm();
                try {
                    quick_window_->toggle_near(
                        nullptr, tray_menu::anchor_for_quick_translation(window_, tray_icon_id), false);
                } catch (const std::exception& error) {
                    notify(MINITRANS_DISPLAY_NAME,
                           L"无法打开快速翻译：" + utf8_to_wide(error.what()));
                }
            }
        } else if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
            POINT point{}; GetCursorPos(&point); show_tray_menu(point);
        }
        return 0;
    }
    case WM_MEASUREITEM:
        if (wparam == 0 && lparam && tray_menu::measure_item(
                *reinterpret_cast<MEASUREITEMSTRUCT*>(lparam))) {
            return TRUE;
        }
        break;
    case WM_DRAWITEM:
        if (wparam == 0 && lparam && tray_menu::draw_item(
                *reinterpret_cast<const DRAWITEMSTRUCT*>(lparam))) {
            return TRUE;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case command_capture: start_capture(); return 0;
        case command_text_translate: open_text_translation(); return 0;
        case command_restore:
            if (last_image_ && !result_.visible()) result_.restore();
            return 0;
        case command_settings: open_settings(); return 0;
        case command_exit: quit(); return 0;
        default: break;
        }
        break;
    case WM_TIMER:
        if (wparam == capture_timer) {
            KillTimer(window_, capture_timer); begin_capture_now(); return 0;
        }
        if (wparam == recapture_timer) {
            KillTimer(window_, recapture_timer); recapture_now(); return 0;
        }
        if (wparam == update_timer) {
            KillTimer(window_, update_timer); start_silent_update_check(); return 0;
        }
        if (wparam == text_translate_timer) {
            KillTimer(window_, text_translate_timer);
            schedule_text_translation(true);
            return 0;
        }
        if (wparam == copy_toast_timer) {
            KillTimer(window_, copy_toast_timer);
            if (text_session_) text_session_->clear_copy_confirmation();
            refresh_text_translation_views();
            return 0;
        }
        break;
    case pipeline_completed_message: process_pipeline_completions(); return 0;
    case silent_update_completed_message: {
        auto update = process_silent_update_check();
        if (update) handle_update_available(*update);
        if (update_self_test_mode_) PostMessageW(window_, WM_CLOSE, 0, 0);
        return 0;
    }
    case settings_update_stage_message:
        settings_.set_update_status(L"更新中...");
        return 0;
    case settings_update_completed_message:
        handle_settings_update_completed(wparam);
        return 0;
    case WM_CLOSE:
        shutting_down_ = true;
        DestroyWindow(window_);
        return 0;
    case WM_DESTROY:
        remove_tray_icon();
        UnregisterHotKey(window_, hotkey_capture);
        UnregisterHotKey(window_, hotkey_toggle);
        UnregisterHotKey(window_, hotkey_text_translate);
        PostQuitMessage(0);
        return 0;
    case WM_NCDESTROY:
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        window_ = nullptr;
        return 0;
    default: break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}


}  // namespace minitrans
