#include "modal_dialog.hpp"

#include "settings_window_internal.hpp"
#include "theme.hpp"
#include "util.hpp"

#include <shellscalingapi.h>
#include <windowsx.h>

namespace minitrans {

ModalDialog::ModalDialog(HINSTANCE instance, HWND owner)
    : instance_(instance), owner_(owner) {}

ModalDialog::~ModalDialog() {
    if (window_ && IsWindow(window_)) {
        // The subclass is already gone: detach the window from it so the
        // messages DestroyWindow sends cannot reach a destroyed object.
        SetWindowLongPtrW(window_, GWLP_USERDATA, 0);
        DestroyWindow(window_);
        window_ = nullptr;
    }
    destroy_theme_resources();
}

void ModalDialog::create_popup(const wchar_t* class_name, const wchar_t* title,
                               int logical_width, int logical_height) {
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = &ModalDialog::window_proc;
    description.hInstance = instance_;
    description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    description.lpszClassName = class_name;
    if (!RegisterClassExW(&description) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw_last_error("register modal dialog");
    }

    HMONITOR monitor = owner_ && IsWindow(owner_)
        ? MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST)
        : MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
    UINT dpi_x = 96;
    UINT dpi_y = 96;
    if (!monitor || FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpi_x,
                                            &dpi_y))) {
        dpi_x = GetDpiForSystem();
    }
    dpi_ = owner_ && IsWindow(owner_) ? static_cast<int>(GetDpiForWindow(owner_))
                                      : static_cast<int>(dpi_x);

    // Borderless popup: the card carries the title, so the system caption bar
    // and its close button are not wanted here.
    constexpr DWORD style = WS_POPUP;
    constexpr DWORD popup_ex_style = WS_EX_TOOLWINDOW | WS_EX_COMPOSITED;
    RECT desired{0, 0, settings_internal::px(logical_width, dpi_),
                 settings_internal::px(logical_height, dpi_)};
    AdjustWindowRectExForDpi(&desired, style, FALSE, popup_ex_style,
                             static_cast<UINT>(dpi_));
    const int width = desired.right - desired.left;
    const int height = desired.bottom - desired.top;

    RECT anchor{};
    MONITORINFO info{sizeof(info)};
    if (owner_ && IsWindow(owner_)) {
        GetWindowRect(owner_, &anchor);
    } else if (monitor && GetMonitorInfoW(monitor, &info)) {
        anchor = info.rcWork;
    } else {
        anchor = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
    }
    const int x = anchor.left + std::max(0L, (anchor.right - anchor.left - width) / 2);
    const int y = anchor.top + std::max(0L, (anchor.bottom - anchor.top - height) / 2);

    window_ = CreateWindowExW(popup_ex_style, class_name, title, style, x, y,
                              width, height, owner_, nullptr, instance_, this);
    if (!window_) {
        if (!window_error_.empty()) throw AppError(window_error_);
        throw_last_error("create modal dialog");
    }
    theme::apply_window_theme(window_, true, true);
}

void ModalDialog::run_modal() {
    ShowWindow(window_, SW_SHOW);
    SetWindowPos(window_, HWND_TOP, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(window_);
    if (const HWND focus = initial_focus()) SetFocus(focus);

    MSG message{};
    while (!closed_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_QUIT) {
            close(false);
            break;
        }
        // Escape closes only this dialog: swallowing both the keydown and the
        // WM_CHAR it produces keeps no leftover keystroke for the owner.
        if (closes_on_escape() &&
            ((message.message == WM_KEYDOWN &&
              static_cast<UINT>(message.wParam) == VK_ESCAPE) ||
             (message.message == WM_CHAR &&
              static_cast<wchar_t>(message.wParam) == 0x1B))) {
            close(false);
            continue;
        }
        const HWND dialog_window = window_;
        if (dialog_window && IsWindow(dialog_window) &&
            IsWindowVisible(dialog_window) &&
            IsDialogMessageW(dialog_window, &message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
}

void ModalDialog::create_theme_resources() {
    destroy_theme_resources();
    font_ = CreateFontW(-settings_internal::px(theme::font_size_body, dpi_), 0, 0,
                        0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                        OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        CLEARTYPE_QUALITY, DEFAULT_PITCH,
                        theme::font_family.c_str());
    background_ = CreateSolidBrush(theme::color_page);
    card_background_ = CreateSolidBrush(theme::color_card);
    input_background_ = CreateSolidBrush(theme::color_input);
}

void ModalDialog::destroy_theme_resources() noexcept {
    if (font_) DeleteObject(font_);
    font_ = nullptr;
    if (background_) DeleteObject(background_);
    background_ = nullptr;
    if (card_background_) DeleteObject(card_background_);
    card_background_ = nullptr;
    if (input_background_) DeleteObject(input_background_);
    input_background_ = nullptr;
}

LRESULT CALLBACK ModalDialog::window_proc(HWND window, UINT message,
                                          WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<ModalDialog*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<ModalDialog*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wparam, lparam);
    try {
        return self->dispatch(message, wparam, lparam);
    } catch (const std::exception& error) {
        self->window_error_ = error.what();
    } catch (...) {
        self->window_error_ = "unexpected modal dialog error";
    }
    if (message == WM_NCCREATE) return FALSE;
    if (message == WM_CREATE) return -1;
    self->close(false);
    return 0;
}

LRESULT ModalDialog::dispatch(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        return 0;
    case WM_CLOSE:
        close(false);
        return 0;
    case WM_NCACTIVATE:
        return DefWindowProcW(window_, message, TRUE, lparam);
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX: {
        const auto dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, theme::color_text);
        SetBkColor(dc, theme::color_input);
        SetBkMode(dc, OPAQUE);
        return reinterpret_cast<LRESULT>(input_background_);
    }
    case WM_COMMAND:
        if (LOWORD(wparam) == IDCANCEL) {
            close(false);
            return 0;
        }
        break;
    case WM_NCDESTROY: {
        const HWND destroyed = window_;
        closed_ = true;
        SetWindowLongPtrW(destroyed, GWLP_USERDATA, 0);
        window_ = nullptr;
        return DefWindowProcW(destroyed, message, wparam, lparam);
    }
    default:
        break;
    }
    return handle_dialog_message(message, wparam, lparam);
}

}  // namespace minitrans
