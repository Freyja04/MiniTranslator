#include "update_window_internal.hpp"

#include "resource.h"
#include "theme.hpp"
#include "util.hpp"

#include <shellscalingapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <string>
#include <utility>

namespace minitrans {
namespace update_internal {

namespace {

class InstallConfirmationDialog {
public:
    bool show(HINSTANCE instance, HWND owner, std::wstring version) {
        instance_ = instance;
        owner_ = owner;
        accent_ = theme::accent;
        prompt_ = std::wstring(
                      L"安装包已通过大小和 SHA-256 校验。\r\n\r\n") +
                  MINITRANS_DISPLAY_NAME + L" 将退出并启动 " +
                  std::move(version) +
                  L" 安装程序。\r\n"
                  L"由于安装包未使用代码签名，Windows 可能显示“未知发布者”。\r\n\r\n"
                  L"是否继续？";

        WNDCLASSEXW description{};
        description.cbSize = sizeof(description);
        description.lpfnWndProc = &InstallConfirmationDialog::window_proc;
        description.style = CS_HREDRAW | CS_VREDRAW;
        description.hInstance = instance_;
        description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        description.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
        description.hIconSm = description.hIcon;
        description.lpszClassName = window_class_name;
        if (!RegisterClassExW(&description) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
            return false;
        }

        RECT anchor{};
        const bool has_owner_rect = owner_ && IsWindow(owner_) &&
                                    GetWindowRect(owner_, &anchor);
        HMONITOR monitor = has_owner_rect
            ? MonitorFromWindow(owner_, MONITOR_DEFAULTTONEAREST)
            : MonitorFromPoint(POINT{}, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO monitor_info{sizeof(monitor_info)};
        if (!GetMonitorInfoW(monitor, &monitor_info)) {
            monitor_info.rcWork = {0, 0, GetSystemMetrics(SM_CXSCREEN),
                                   GetSystemMetrics(SM_CYSCREEN)};
        }
        if (!has_owner_rect) anchor = monitor_info.rcWork;

        dpi_ = owner_ && IsWindow(owner_)
            ? static_cast<int>(GetDpiForWindow(owner_))
            : 0;
        if (dpi_ <= 0) {
            UINT dpi_x = 96;
            UINT dpi_y = 96;
            if (FAILED(GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI,
                                        &dpi_x, &dpi_y))) {
                dpi_x = GetDpiForSystem();
            }
            dpi_ = static_cast<int>(dpi_x ? dpi_x : 96);
        }
        if (!create_theme_resources()) return false;

        const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
        constexpr DWORD ex_style = WS_EX_DLGMODALFRAME;
        RECT desired{0, 0, px(520, dpi_), px(332, dpi_)};
        AdjustWindowRectExForDpi(&desired, style, FALSE, ex_style,
                                 static_cast<UINT>(dpi_));
        const int width = desired.right - desired.left;
        const int height = desired.bottom - desired.top;
        const LONG centered_x = anchor.left +
            ((anchor.right - anchor.left) - width) / 2;
        const LONG centered_y = anchor.top +
            ((anchor.bottom - anchor.top) - height) / 2;
        const LONG x = std::clamp(centered_x, monitor_info.rcWork.left,
                                  std::max(monitor_info.rcWork.left,
                                           monitor_info.rcWork.right - width));
        const LONG y = std::clamp(centered_y, monitor_info.rcWork.top,
                                  std::max(monitor_info.rcWork.top,
                                           monitor_info.rcWork.bottom - height));

        window_ = CreateWindowExW(
            ex_style, window_class_name, L"安装更新", style,
            x, y, width, height, owner_, nullptr, instance_, this);
        if (!window_) {
            destroy_theme_resources();
            return false;
        }
        theme::apply_window_theme(window_, true, true);

        disabled_owner_ = owner_ && IsWindow(owner_) && IsWindowEnabled(owner_);
        if (disabled_owner_) EnableWindow(owner_, FALSE);
        ShowWindow(window_, SW_SHOW);
        ::UpdateWindow(window_);
        SetForegroundWindow(window_);
        if (cancel_button_) SetFocus(cancel_button_);

        bool repost_quit = false;
        int quit_code = 0;
        MSG message{};
        while (!finished_) {
            const BOOL status = GetMessageW(&message, nullptr, 0, 0);
            if (status <= 0) {
                if (status == 0) {
                    repost_quit = true;
                    quit_code = static_cast<int>(message.wParam);
                }
                finished_ = true;
                break;
            }
            const bool belongs_to_dialog =
                message.hwnd == window_ || (window_ && IsChild(window_, message.hwnd));
            if (belongs_to_dialog && message.message == WM_KEYDOWN) {
                if (message.wParam == VK_ESCAPE) {
                    finish(false);
                    continue;
                }
                if (message.wParam == VK_RETURN) {
                    finish(GetFocus() == confirm_button_);
                    continue;
                }
            }
            if (!window_ || !IsDialogMessageW(window_, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }

        const bool accepted = accepted_ && !repost_quit;
        if (window_ && IsWindow(window_)) DestroyWindow(window_);
        window_ = nullptr;
        confirm_button_ = nullptr;
        cancel_button_ = nullptr;
        if (disabled_owner_ && owner_ && IsWindow(owner_)) {
            EnableWindow(owner_, TRUE);
            SetForegroundWindow(owner_);
        }
        disabled_owner_ = false;
        destroy_theme_resources();
        if (repost_quit) PostQuitMessage(quit_code);
        return accepted;
    }

private:
    inline static constexpr wchar_t window_class_name[] =
        L"MiniTranslator.Native.InstallConfirmation.v1";
    static constexpr int id_confirm = 8201;

    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam) {
        auto* self = reinterpret_cast<InstallConfirmationDialog*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
            self = static_cast<InstallConfirmationDialog*>(create->lpCreateParams);
            self->window_ = window;
            SetWindowLongPtrW(window, GWLP_USERDATA,
                              reinterpret_cast<LONG_PTR>(self));
        }
        try {
            return self ? self->handle_message(message, wparam, lparam)
                        : DefWindowProcW(window, message, wparam, lparam);
        } catch (...) {
            return message == WM_NCCREATE ? FALSE : message == WM_CREATE ? -1 : 0;
        }
    }

    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
        switch (message) {
        case WM_CREATE:
            return create_controls() ? 0 : -1;
        case WM_SIZE:
            layout_controls();
            InvalidateRect(window_, nullptr, FALSE);
            return 0;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(wparam);
            const auto* suggested = reinterpret_cast<const RECT*>(lparam);
            SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                         suggested->right - suggested->left,
                         suggested->bottom - suggested->top,
                         SWP_NOACTIVATE | SWP_NOZORDER);
            create_theme_resources();
            set_font(confirm_button_, font_);
            set_font(cancel_button_, font_);
            layout_controls();
            RedrawWindow(window_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE |
                             RDW_UPDATENOW);
            return 0;
        }
        case WM_COMMAND:
            if (HIWORD(wparam) == BN_CLICKED) {
                if (LOWORD(wparam) == id_confirm) {
                    finish(true);
                    return 0;
                }
                if (LOWORD(wparam) == IDCANCEL) {
                    finish(false);
                    return 0;
                }
            }
            break;
        case WM_DRAWITEM:
            if (const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam)) {
                return draw_button(*item);
            }
            return FALSE;
        case WM_PAINT:
            paint();
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_KEYDOWN:
            if (wparam == VK_ESCAPE) {
                finish(false);
                return 0;
            }
            break;
        case WM_CLOSE:
            finish(false);
            return 0;
        case WM_NCDESTROY: {
            const HWND destroyed = window_;
            finished_ = true;
            SetWindowLongPtrW(destroyed, GWLP_USERDATA, 0);
            window_ = nullptr;
            return DefWindowProcW(destroyed, message, wparam, lparam);
        }
        default:
            break;
        }
        return DefWindowProcW(window_, message, wparam, lparam);
    }

    bool create_theme_resources() {
        destroy_theme_resources();
        const auto make_font = [&](int pixels, int weight) {
            return CreateFontW(-px(pixels, dpi_), 0, 0, 0, weight,
                               FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                               OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH,
                               theme::font_family.c_str());
        };
        font_ = make_font(theme::font_size_body, FW_NORMAL);
        title_font_ = make_font(theme::font_size_title, FW_SEMIBOLD);
        small_font_ = make_font(theme::font_size_small, FW_NORMAL);
        background_ = CreateSolidBrush(color_page);
        const bool ready = font_ && title_font_ && small_font_ && background_;
        if (!ready) destroy_theme_resources();
        return ready;
    }

    void destroy_theme_resources() noexcept {
        for (auto** font : {&font_, &title_font_, &small_font_}) {
            if (*font) DeleteObject(*font);
            *font = nullptr;
        }
        if (background_) DeleteObject(background_);
        background_ = nullptr;
    }

    bool create_controls() {
        const auto make_button = [&](const wchar_t* text, int identifier) {
            HWND button = CreateWindowExW(
                0, L"BUTTON", text,
                WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                0, 0, 1, 1, window_,
                reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)),
                instance_, nullptr);
            set_font(button, font_);
            return button;
        };
        confirm_button_ = make_button(L"继续安装", id_confirm);
        cancel_button_ = make_button(L"取消", IDCANCEL);
        if (!confirm_button_ || !cancel_button_) return false;
        layout_controls();
        return true;
    }

    void layout_controls() const noexcept {
        if (!window_) return;
        RECT client{};
        GetClientRect(window_, &client);
        const int margin = px(22, dpi_);
        const int bottom = client.bottom - px(16, dpi_);
        const int height = px(36, dpi_);
        const int cancel_width = px(92, dpi_);
        const int confirm_width = px(122, dpi_);
        const int gap = px(10, dpi_);
        const int cancel_left = client.right - margin - cancel_width;
        if (cancel_button_) {
            SetWindowPos(cancel_button_, nullptr, cancel_left, bottom - height,
                         cancel_width, height, SWP_NOACTIVATE | SWP_NOZORDER);
        }
        if (confirm_button_) {
            SetWindowPos(confirm_button_, nullptr,
                         cancel_left - gap - confirm_width, bottom - height,
                         confirm_width, height, SWP_NOACTIVATE | SWP_NOZORDER);
        }
    }

    void paint() const {
        PAINTSTRUCT state{};
        HDC target = BeginPaint(window_, &state);
        RECT client{};
        GetClientRect(window_, &client);
        HDC dc = CreateCompatibleDC(target);
        HBITMAP bitmap = CreateCompatibleBitmap(
            target, std::max(1L, client.right), std::max(1L, client.bottom));
        if (!dc || !bitmap) {
            if (bitmap) DeleteObject(bitmap);
            if (dc) DeleteDC(dc);
            FillRect(target, &client, background_);
            EndPaint(window_, &state);
            return;
        }
        const auto old_bitmap = SelectObject(dc, bitmap);
        FillRect(dc, &client, background_);

        const int margin = px(22, dpi_);
        RECT icon{margin, px(20, dpi_), margin + px(42, dpi_), px(62, dpi_)};
        fill_round_rect(dc, icon, px(21, dpi_), color_card, color_warn);
        draw_text(dc, L"!", icon, title_font_, color_warn,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        const int heading_left = icon.right + px(14, dpi_);
        draw_text(dc, L"准备安装更新",
                  RECT{heading_left, px(17, dpi_), client.right - margin,
                       px(43, dpi_)},
                  title_font_, color_text,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        draw_text(dc, L"请确认后再退出当前版本",
                  RECT{heading_left, px(43, dpi_), client.right - margin,
                       px(64, dpi_)},
                  small_font_, color_text_dim,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE);

        RECT card{margin, px(78, dpi_), client.right - margin, px(254, dpi_)};
        fill_round_rect(dc, card, px(8, dpi_), color_card, color_line);
        draw_text(dc, prompt_,
                  RECT{card.left + px(17, dpi_), card.top + px(14, dpi_),
                       card.right - px(17, dpi_), card.bottom - px(13, dpi_)},
                  font_, color_text,
                  DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL);

        HPEN separator = CreatePen(PS_SOLID, 1, color_line);
        const auto old_pen = SelectObject(dc, separator);
        MoveToEx(dc, margin, px(270, dpi_), nullptr);
        LineTo(dc, client.right - margin, px(270, dpi_));
        SelectObject(dc, old_pen);
        DeleteObject(separator);

        BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
        SelectObject(dc, old_bitmap);
        DeleteObject(bitmap);
        DeleteDC(dc);
        EndPaint(window_, &state);
    }

    LRESULT draw_button(const DRAWITEMSTRUCT& item) const {
        if (item.CtlType != ODT_BUTTON) return FALSE;
        RECT rect = item.rcItem;
        const bool primary = item.hwndItem == confirm_button_;
        const bool disabled = (item.itemState & ODS_DISABLED) != 0;
        const bool pressed = (item.itemState & ODS_SELECTED) != 0;
        const bool hot = (item.itemState & ODS_HOTLIGHT) != 0;
        const bool focused = (item.itemState & ODS_FOCUS) != 0;
        FillRect(item.hDC, &rect, background_);

        COLORREF fill = color_page;
        COLORREF border = color_line_high;
        COLORREF foreground = color_text_dim;
        if (disabled) {
            fill = color_card;
            border = color_line;
            foreground = color_text_faint;
        } else if (primary) {
            fill = pressed ? adjust_rgb(accent_, -12)
                           : hot ? adjust_rgb(accent_, 18) : accent_;
            border = fill;
            foreground = theme::accent_foreground;
        } else {
            fill = pressed ? theme::color_hover
                           : hot ? theme::color_hover : color_page;
            foreground = hot || focused ? color_text : color_text_dim;
        }
        fill_round_rect(item.hDC, rect, px(6, dpi_), fill,
                        focused ? accent_ : border);
        const auto label = text_of(item.hwndItem);
        draw_text(item.hDC, label, rect, font_, foreground,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (focused) {
            RECT focus = rect;
            InflateRect(&focus, -px(3, dpi_), -px(3, dpi_));
            DrawFocusRect(item.hDC, &focus);
        }
        return TRUE;
    }

    void finish(bool accepted) noexcept {
        accepted_ = accepted;
        finished_ = true;
        if (window_) ShowWindow(window_, SW_HIDE);
    }

    HINSTANCE instance_{};
    HWND owner_{};
    HWND window_{};
    HWND confirm_button_{};
    HWND cancel_button_{};
    HFONT font_{};
    HFONT title_font_{};
    HFONT small_font_{};
    HBRUSH background_{};
    std::wstring prompt_;
    COLORREF accent_{};
    int dpi_{96};
    bool accepted_{};
    bool finished_{};
    bool disabled_owner_{};
};

}  // namespace

bool show_install_confirmation(HINSTANCE instance, HWND owner,
                               std::wstring version) {
    InstallConfirmationDialog dialog;
    return dialog.show(instance, owner, std::move(version));
}

}  // namespace update_internal
}  // namespace minitrans
