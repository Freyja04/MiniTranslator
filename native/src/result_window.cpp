#include "result_window.hpp"
#include "result_window_internal.hpp"

#include "capture.hpp"
#include "resource.h"
#include "theme.hpp"
#include "util.hpp"

#include <dwmapi.h>
#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace minitrans {

namespace {

constexpr wchar_t class_name[] = L"MiniTranslator.Native.ResultWindow.v1";
constexpr int logical_border_hit = 8;
constexpr UINT_PTR progress_timer = 5;
constexpr UINT_PTR recapture_restore_timer = 6;

bool equal_rect(const RECT& left, const RECT& right) {
    return left.left == right.left && left.top == right.top
        && left.right == right.right && left.bottom == right.bottom;
}

bool equal_size(const RECT& left, const RECT& right) {
    return left.right - left.left == right.right - right.left
        && left.bottom - left.top == right.bottom - right.top;
}

}  // namespace

using result_window_internal::dpi_value;
using result_window_internal::resize_transparent_key;

ResultWindow::ResultWindow(HINSTANCE instance) : instance_(instance) {}

ResultWindow::~ResultWindow() { close(); }

void ResultWindow::create_window() {
    if (window_) return;
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = &ResultWindow::window_proc;
    description.style = CS_DBLCLKS;
    description.hInstance = instance_;
    description.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    description.hIconSm = description.hIcon;
    description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    description.hbrBackground = nullptr;
    description.lpszClassName = class_name;
    const ATOM atom = RegisterClassExW(&description);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw_last_error("register result window");
    }
    window_ = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED,
                              class_name, MINITRANS_DISPLAY_NAME,
                              WS_POPUP | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                              0, 0, 1, 1, nullptr, nullptr, instance_, this);
    if (!window_) throw_last_error("create result window");
    SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
    create_control_bar();
}

void ResultWindow::show_loading(const RECT& physical_rect,
                                std::shared_ptr<const PixelBuffer> original,
                                ResultAppearance appearance) {
    create_window();
    if (refreshing_) {
        KillTimer(window_, recapture_restore_timer);
        SetTimer(window_, progress_timer, 16, nullptr);
        pending_source_ = PendingSource{
            physical_rect, std::move(original), std::move(appearance),
            {physical_rect.left, physical_rect.top}};
        recapturing_ = false;
        editing_ = false;
        error_.clear();
        handles_alpha_ = 0.0;
        set_capture_exclusion(false);
        InvalidateRect(window_, nullptr, FALSE);
        UpdateWindow(window_);
        SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
        sync_control_bar();
        if (control_bar_) InvalidateRect(control_bar_, nullptr, FALSE);
        return;
    }

    KillTimer(window_, recapture_restore_timer);
    recapturing_ = false;
    pending_source_.reset();
    refresh_surface_ = {};
    const bool same_source = original_ && original && original_.get() == original.get();
    original_ = std::move(original);
    translated_surface_ = {};
    appearance_ = std::move(appearance);
    result_ = {};
    plain_text_.clear();
    error_.clear();
    captured_rect_ = physical_rect;
    image_screen_origin_ = {physical_rect.left, physical_rect.top};
    if (!home_initialized_) {
        home_rect_ = physical_rect;
        home_initialized_ = true;
        const int selected_width = std::max(
            1, static_cast<int>(physical_rect.right - physical_rect.left));
        const int selected_height = std::max(
            1, static_cast<int>(physical_rect.bottom - physical_rect.top));
        minimum_width_ = std::min(selected_width, dpi_value(window_, 72));
        minimum_height_ = std::min(selected_height, dpi_value(window_, 32));
    }
    if (!same_source) editable_result_ = false;
    loading_ = true;
    progress_phase_ = 0.0;
    SetTimer(window_, progress_timer, 16, nullptr);
    editing_ = false;
    peek_ = false;
    closing_ = false;
    handles_alpha_ = 0.0;
    const int width = std::max(1, static_cast<int>(physical_rect.right - physical_rect.left));
    const int height = std::max(1, static_cast<int>(physical_rect.bottom - physical_rect.top));
    SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
    SetWindowPos(window_, HWND_TOPMOST, physical_rect.left, physical_rect.top,
                 width, height, SWP_SHOWWINDOW);
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
    SetFocus(window_);
    update_window_region();
    sync_control_bar();
    if (control_bar_) InvalidateRect(control_bar_, nullptr, FALSE);
    InvalidateRect(window_, nullptr, FALSE);
    UpdateWindow(window_);
    set_capture_exclusion(false);
}

void ResultWindow::show_retry_loading(ResultAppearance appearance) {
    if (!window_) return;
    KillTimer(window_, recapture_restore_timer);
    recapturing_ = false;
    if (pending_source_) {
        pending_source_->appearance = std::move(appearance);
        refreshing_ = true;
        loading_ = false;
        editing_ = false;
        error_.clear();
        handles_alpha_ = 0.0;
        SetTimer(window_, progress_timer, 16, nullptr);
        InvalidateRect(window_, nullptr, FALSE);
        UpdateWindow(window_);
        sync_control_bar();
        if (control_bar_) InvalidateRect(control_bar_, nullptr, FALSE);
        return;
    }

    appearance_ = std::move(appearance);
    loading_ = true;
    SetTimer(window_, progress_timer, 16, nullptr);
    editing_ = false;
    peek_ = false;
    error_.clear();
    handles_alpha_ = 0.0;
    SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
    sync_control_bar();
    if (control_bar_) InvalidateRect(control_bar_, nullptr, FALSE);
    InvalidateRect(window_, nullptr, FALSE);
    UpdateWindow(window_);
}

void ResultWindow::set_result(PipelineResult result) {
    if (!window_) return;
    KillTimer(window_, progress_timer);
    KillTimer(window_, recapture_restore_timer);
    const bool commit_refresh = refreshing_ && pending_source_.has_value();
    if (commit_refresh) {
        original_ = std::move(pending_source_->original);
        appearance_ = std::move(pending_source_->appearance);
        captured_rect_ = pending_source_->rect;
        image_screen_origin_ = pending_source_->image_origin;
        recapturing_ = false;
        if (!resizing_) set_capture_exclusion(false);
    }
    result_ = std::move(result);
    build_repaired_background();
    plain_text_ = result_.plain_text;
    loading_ = false;
    editable_result_ = !result_.blocks.empty();
    error_.clear();
    refreshing_ = false;
    pending_source_.reset();
    refresh_surface_ = {};
    RedrawWindow(window_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW);
    SetLayeredWindowAttributes(
        window_, resizing_ ? resize_transparent_key : 0, 255,
        resizing_ ? (LWA_ALPHA | LWA_COLORKEY) : LWA_ALPHA);
    sync_control_bar();
    if (control_bar_) InvalidateRect(control_bar_, nullptr, FALSE);
}

void ResultWindow::set_error(std::wstring message) {
    if (!window_) return;
    KillTimer(window_, progress_timer);
    KillTimer(window_, recapture_restore_timer);
    if (refreshing_) {
        recapturing_ = false;
        refreshing_ = false;
        set_capture_exclusion(false);
        SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
        loading_ = false;
        editing_ = false;
        handles_alpha_ = 0.0;
        error_.clear();
        pending_source_.reset();
        refresh_surface_ = {};
        const std::wstring diagnostic = L"MiniTranslator refresh failed: " + message + L"\n";
        OutputDebugStringW(diagnostic.c_str());
        InvalidateRect(window_, nullptr, FALSE);
        UpdateWindow(window_);
        sync_control_bar();
        if (control_bar_) InvalidateRect(control_bar_, nullptr, FALSE);
        return;
    }
    if (recapturing_) {
        recapturing_ = false;
        set_capture_exclusion(false);
        SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
        sync_control_bar();
    }
    translated_surface_ = {};
    loading_ = false;
    error_ = std::move(message);
    InvalidateRect(window_, nullptr, FALSE);
    if (control_bar_) InvalidateRect(control_bar_, nullptr, FALSE);
}

void ResultWindow::refresh_appearance(ResultAppearance appearance) {
    if (!window_) return;
    if (refreshing_ && pending_source_) {
        pending_source_->appearance = appearance;
    }
    appearance_ = std::move(appearance);
    if (!peek_ && !result_.blocks.empty()) build_repaired_background();
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE);
}

void ResultWindow::close() {
    if (!window_ || closing_) return;
    closing_ = true;
    KillTimer(window_, progress_timer);
    content_dragging_ = false;
    destroy_control_bar();
    const HWND old = window_;
    DestroyWindow(old);
    window_ = nullptr;
    release_paint_buffer();
    original_.reset();
    pending_source_.reset();
    refresh_surface_ = {};
    translated_surface_ = {};
    result_ = {};
    plain_text_.clear();
    error_.clear();
    editable_result_ = false;
    home_initialized_ = false;
    resizing_ = false;
    recapturing_ = false;
    refreshing_ = false;
    refresh_was_active_on_resize_ = false;
    recapture_exclusion_active_ = false;
    control_bar_capture_excluded_ = false;
    content_dragging_ = false;
    freeze_content_only_ = false;
    handles_alpha_ = 0.0;
    progress_phase_ = 0.0;
    const auto callback = closed_callback_;
    closing_ = false;
    if (callback) callback();
}

void ResultWindow::minimize() {
    if (window_) {
        if (!recapturing_) KillTimer(window_, recapture_restore_timer);
        ShowWindow(window_, SW_HIDE);
        if (control_bar_) ShowWindow(control_bar_, SW_HIDE);
        SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
        content_dragging_ = false;
        if (GetCapture() == window_) ReleaseCapture();
    }
}

void ResultWindow::restore() {
    if (!window_) return;
    ShowWindow(window_, SW_SHOW);
    SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(window_);
    SetFocus(window_);
    sync_control_bar();
}

void ResultWindow::set_editing(bool value) {
    editing_ = value;
    if (!window_) return;
    if (control_bar_) InvalidateRect(control_bar_, nullptr, FALSE);
}

bool ResultWindow::visible() const noexcept {
    return window_ && IsWindowVisible(window_);
}

void ResultWindow::set_peek(bool value) {
    if (refreshing_ || !refresh_surface_.empty()) return;
    if (peek_ == value) return;
    peek_ = value;
    RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
}

void ResultWindow::copy_all() {
    if (copy_callback_) copy_callback_(plain_text_);
}

void ResultWindow::move_by(int dx, int dy) {
    if (!window_ || recapturing_ || resizing_ || (dx == 0 && dy == 0)) {
        return;
    }
    RECT rect{};
    GetWindowRect(window_, &rect);
    image_screen_origin_.x += dx;
    image_screen_origin_.y += dy;
    if (pending_source_) {
        pending_source_->image_origin.x += dx;
        pending_source_->image_origin.y += dy;
    }
    SetWindowPos(window_, HWND_TOPMOST, rect.left + dx, rect.top + dy, 0, 0,
                 SWP_NOSIZE | SWP_NOACTIVATE);
    InvalidateRect(window_, nullptr, FALSE);
}

void ResultWindow::reset_geometry() {
    if (!window_ || !home_initialized_ || resizing_) return;
    RECT current{};
    GetWindowRect(window_, &current);
    if (equal_rect(current, home_rect_) && equal_rect(captured_rect_, home_rect_)) return;
    const bool needs_recapture = !equal_rect(captured_rect_, home_rect_);
    if (!needs_recapture) {
        set_capture_exclusion(false);
        refresh_surface_ = {};
        refreshing_ = false;
        recapturing_ = false;
        pending_source_.reset();
        image_screen_origin_ = {home_rect_.left, home_rect_.top};
        SetWindowPos(window_, HWND_TOPMOST, home_rect_.left, home_rect_.top,
                     home_rect_.right - home_rect_.left,
                     home_rect_.bottom - home_rect_.top, SWP_SHOWWINDOW);
        update_window_region();
        sync_control_bar();
        RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        return;
    }
    freeze_current_surface();
    refreshing_ = true;
    pending_source_.reset();
    if (control_bar_) ShowWindow(control_bar_, SW_HIDE);
    image_screen_origin_ = {home_rect_.left, home_rect_.top};
    SetWindowPos(window_, HWND_TOPMOST, home_rect_.left, home_rect_.top,
                 home_rect_.right - home_rect_.left,
                 home_rect_.bottom - home_rect_.top, SWP_SHOWWINDOW);
    update_window_region();
    if (recapture_callback_) {
        recapturing_ = true;
        handles_alpha_ = 0.0;
        set_capture_exclusion(false);
        SetLayeredWindowAttributes(window_, 0, 0, LWA_ALPHA);
        DwmFlush();
        SetTimer(window_, recapture_restore_timer, 1000, nullptr);
        recapture_callback_(home_rect_);
    }
}

void ResultWindow::freeze_current_surface() noexcept {
    if (!window_ || !IsWindowVisible(window_) || !refresh_surface_.empty()) {
        return;
    }
    const double previous_handles_alpha = handles_alpha_;
    handles_alpha_ = 0.0;
    freeze_content_only_ = true;
    if (control_bar_) ShowWindow(control_bar_, SW_HIDE);
    RedrawWindow(window_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_UPDATENOW);
    DwmFlush();
    RECT bounds{};
    if (!GetWindowRect(window_, &bounds) || bounds.right <= bounds.left
        || bounds.bottom <= bounds.top) {
        freeze_content_only_ = false;
        handles_alpha_ = previous_handles_alpha;
        return;
    }
    try {
        refresh_surface_ = capture_rect(bounds);
    } catch (...) {
        if (!translated_surface_.empty()) {
            refresh_surface_ = translated_surface_;
        } else if (original_ && !original_->empty()) {
            refresh_surface_ = *original_;
        }
        OutputDebugStringW(L"MiniTranslator: could not freeze the current result surface.\n");
    }
    freeze_content_only_ = false;
    handles_alpha_ = previous_handles_alpha;
}

void ResultWindow::aim_handles(double target) {
    const double value = recapturing_ ? 0.0 : std::clamp(target, 0.0, 1.0);
    if (std::abs(value - handles_alpha_) <= 0.01) return;
    handles_alpha_ = value;
    if (window_ && IsWindowVisible(window_)) {
        InvalidateRect(window_, nullptr, FALSE);
    }
}

bool ResultWindow::set_capture_exclusion(bool exclude) noexcept {
    if (!window_) return false;
    if (!exclude) {
        if (!recapture_exclusion_active_ && !control_bar_capture_excluded_) return true;
        SetWindowDisplayAffinity(window_, WDA_NONE);
        if (control_bar_) SetWindowDisplayAffinity(control_bar_, WDA_NONE);
        recapture_exclusion_active_ = false;
        control_bar_capture_excluded_ = false;
        return true;
    }

    const bool main_excluded =
        SetWindowDisplayAffinity(window_, WDA_EXCLUDEFROMCAPTURE) != FALSE;
    const bool bar_excluded = !control_bar_ ||
        SetWindowDisplayAffinity(control_bar_, WDA_EXCLUDEFROMCAPTURE) != FALSE;
    recapture_exclusion_active_ = main_excluded;
    control_bar_capture_excluded_ = bar_excluded;
    if (!main_excluded) {
        if (control_bar_) SetWindowDisplayAffinity(control_bar_, WDA_NONE);
        control_bar_capture_excluded_ = false;
        return false;
    }
    // The affinity must reach the compositor before GDI captures the desktop.
    DwmFlush();
    return true;
}

LRESULT CALLBACK ResultWindow::window_proc(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<ResultWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<ResultWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handle_message(message, wparam, lparam)
                : DefWindowProcW(window, message, wparam, lparam);
}


LRESULT ResultWindow::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint(); return 0;
    case theme::changed_message:
        destroy_control_bar();
        create_control_bar();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_MOVE:
        sync_control_bar();
        return 0;
    case WM_SIZE:
        update_window_region();
        sync_control_bar();
        if (resizing_) {
            RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_NOERASE);
        } else {
            InvalidateRect(window_, nullptr, FALSE);
        }
        return 0;
    case WM_MOUSEMOVE: {
        if (content_dragging_) {
            POINT cursor{};
            if (GetCursorPos(&cursor)) {
                move_by(cursor.x - content_drag_cursor_.x,
                        cursor.y - content_drag_cursor_.y);
                content_drag_cursor_ = cursor;
            }
        }
        aim_handles(1.0);
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, window_, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (!resizing_) aim_handles(0.0);
        return 0;
    case WM_LBUTTONDOWN:
        SetFocus(window_);
        if (!recapturing_ && !resizing_ && GetCursorPos(&content_drag_cursor_)) {
            content_dragging_ = true;
            SetCapture(window_);
        }
        return 0;
    case WM_LBUTTONUP:
        if (content_dragging_) {
            content_dragging_ = false;
            if (GetCapture() == window_) ReleaseCapture();
        }
        return 0;
    case WM_SETCURSOR:
        if (LOWORD(lparam) == HTCLIENT && !recapturing_) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return TRUE;
        }
        break;
    case WM_COMMAND:
        switch (LOWORD(wparam)) {
        case 4102:
            if (!loading_ && !refreshing_ && retry_callback_) retry_callback_();
            return 0;
        case 4103:
            if (editable_result_ && !loading_ && !refreshing_ &&
                !pending_source_ && !editing_ && edit_callback_) {
                edit_callback_();
            }
            return 0;
        case 4104: minimize(); return 0;
        case 4105: close(); return 0;
        default: break;
        }
        break;
    case WM_KEYDOWN:
        {
        const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
        const int step = (GetKeyState(VK_SHIFT) & 0x8000) != 0 ? 10 : 1;
        if (wparam == VK_ESCAPE) { close(); return 0; }
        if (wparam == L'R' && !ctrl && !loading_ && !refreshing_ && retry_callback_) {
            retry_callback_();
            return 0;
        }
        if (wparam == L'E' && !ctrl && editable_result_ && !loading_ &&
            !refreshing_ && !pending_source_ && !editing_ && edit_callback_) {
            edit_callback_();
            return 0;
        }
        if (wparam == L'M' || wparam == VK_OEM_MINUS) { minimize(); return 0; }
        if (wparam == VK_SPACE) { set_peek(true); return 0; }
        if (ctrl && wparam == L'C') {
            copy_all();
            return 0;
        }
        if (ctrl && wparam == L'A') {
            copy_all();
            return 0;
        }
        if (wparam == VK_LEFT || wparam == VK_RIGHT ||
            wparam == VK_UP || wparam == VK_DOWN) {
            int dx = 0, dy = 0;
            if (wparam == VK_LEFT) dx = -step;
            if (wparam == VK_RIGHT) dx = step;
            if (wparam == VK_UP) dy = -step;
            if (wparam == VK_DOWN) dy = step;
            move_by(dx, dy);
            return 0;
        }
        if (wparam == VK_HOME) {
            reset_geometry();
            return 0;
        }
        }
        break;
    case WM_KEYUP:
        if (wparam == VK_SPACE) { set_peek(false); return 0; }
        break;
    case WM_RBUTTONDOWN: set_peek(true); SetCapture(window_); return 0;
    case WM_RBUTTONUP: ReleaseCapture(); set_peek(false); return 0;
    case WM_CONTEXTMENU: return 0;
    case WM_CANCELMODE:
        content_dragging_ = false;
        if (GetCapture() == window_) ReleaseCapture();
        if (peek_) set_peek(false);
        return 0;
    case WM_CAPTURECHANGED:
        if (content_dragging_) {
            content_dragging_ = false;
        }
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lparam);
        info->ptMinTrackSize.x = std::max(1, minimum_width_);
        info->ptMinTrackSize.y = std::max(1, minimum_height_);
        return 0;
    }
    case WM_NCHITTEST: {
        const LRESULT base = DefWindowProcW(window_, message, wparam, lparam);
        if (base != HTCLIENT) return base;
        if (recapturing_) return HTCLIENT;
        RECT window_rect{};
        GetWindowRect(window_, &window_rect);
        const int x = GET_X_LPARAM(lparam) - window_rect.left;
        const int y = GET_Y_LPARAM(lparam) - window_rect.top;
        const int width = window_rect.right - window_rect.left;
        const int height = window_rect.bottom - window_rect.top;
        const int edge = dpi_value(window_, logical_border_hit);
        const bool left = x < edge, right = x >= width - edge;
        const bool top = y < edge, bottom = y >= height - edge;
        if (top && left) return HTTOPLEFT;
        if (top && right) return HTTOPRIGHT;
        if (bottom && left) return HTBOTTOMLEFT;
        if (bottom && right) return HTBOTTOMRIGHT;
        if (left) return HTLEFT;
        if (right) return HTRIGHT;
        if (top) return HTTOP;
        if (bottom) return HTBOTTOM;
        return HTCLIENT;
    }
    case WM_ENTERSIZEMOVE: {
        GetWindowRect(window_, &resize_start_rect_);
        refresh_was_active_on_resize_ =
            refreshing_ || pending_source_.has_value();
        resizing_ = true;
        content_dragging_ = false;
        handles_alpha_ = 1.0;
        set_capture_exclusion(false);
        SetLayeredWindowAttributes(window_, resize_transparent_key, 255,
                                   LWA_ALPHA | LWA_COLORKEY);
        sync_control_bar();
        RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        return 0;
    }
    case WM_SIZING: {
        auto* rect = reinterpret_cast<RECT*>(lparam);
        const bool moves_left = wparam == WMSZ_LEFT || wparam == WMSZ_TOPLEFT
                             || wparam == WMSZ_BOTTOMLEFT;
        const bool moves_right = wparam == WMSZ_RIGHT || wparam == WMSZ_TOPRIGHT
                              || wparam == WMSZ_BOTTOMRIGHT;
        const bool moves_top = wparam == WMSZ_TOP || wparam == WMSZ_TOPLEFT
                            || wparam == WMSZ_TOPRIGHT;
        const bool moves_bottom = wparam == WMSZ_BOTTOM || wparam == WMSZ_BOTTOMLEFT
                               || wparam == WMSZ_BOTTOMRIGHT;
        const bool corner = wparam == WMSZ_TOPLEFT || wparam == WMSZ_TOPRIGHT
                         || wparam == WMSZ_BOTTOMLEFT || wparam == WMSZ_BOTTOMRIGHT;
        if (corner && (GetKeyState(VK_SHIFT) & 0x8000) != 0) {
            const int base_width = std::max(
                1, static_cast<int>(resize_start_rect_.right - resize_start_rect_.left));
            const int base_height = std::max(
                1, static_cast<int>(resize_start_rect_.bottom - resize_start_rect_.top));
            int width = std::max(minimum_width_, static_cast<int>(rect->right - rect->left));
            int height = std::max(minimum_height_, static_cast<int>(rect->bottom - rect->top));
            const double denominator = static_cast<double>(base_width) * base_width
                                     + static_cast<double>(base_height) * base_height;
            double scale = 1.0
                + (static_cast<double>(width - base_width) * base_width
                   + static_cast<double>(height - base_height) * base_height)
                    / denominator;
            scale = std::max({scale,
                              static_cast<double>(minimum_width_) / base_width,
                              static_cast<double>(minimum_height_) / base_height});
            width = std::max(minimum_width_,
                             static_cast<int>(std::lround(base_width * scale)));
            height = std::max(minimum_height_,
                              static_cast<int>(std::lround(base_height * scale)));
            if (wparam == WMSZ_TOPLEFT || wparam == WMSZ_BOTTOMLEFT) rect->left = rect->right - width;
            else rect->right = rect->left + width;
            if (wparam == WMSZ_TOPLEFT || wparam == WMSZ_TOPRIGHT) rect->top = rect->bottom - height;
            else rect->bottom = rect->top + height;
        }
        MONITORINFO monitor{};
        monitor.cbSize = sizeof(monitor);
        if (GetMonitorInfoW(MonitorFromRect(&resize_start_rect_, MONITOR_DEFAULTTONEAREST),
                            &monitor)) {
            if (moves_left) rect->left = std::max(rect->left, monitor.rcMonitor.left);
            if (moves_right) rect->right = std::min(rect->right, monitor.rcMonitor.right);
            if (moves_top) rect->top = std::max(rect->top, monitor.rcMonitor.top);
            if (moves_bottom) rect->bottom = std::min(rect->bottom, monitor.rcMonitor.bottom);
        }
        return TRUE;
    }
    case WM_EXITSIZEMOVE: {
        RECT rect{}; GetWindowRect(window_, &rect);
        resizing_ = false;
        POINT cursor{};
        GetCursorPos(&cursor);
        aim_handles(PtInRect(&rect, cursor) ? 1.0 : 0.0);
        const bool region_changed = !equal_rect(rect, resize_start_rect_)
                                 || !equal_size(rect, captured_rect_);
        if (recapture_callback_ && region_changed) {
            refreshing_ = true;
            recapturing_ = true;
            pending_source_.reset();
            handles_alpha_ = 0.0;
            set_capture_exclusion(false);
            if (control_bar_) ShowWindow(control_bar_, SW_HIDE);
            SetLayeredWindowAttributes(window_, 0, 0, LWA_ALPHA);
            DwmFlush();
            SetTimer(window_, recapture_restore_timer, 1000, nullptr);
            recapture_callback_(rect);
        } else if (refresh_was_active_on_resize_
                   && (refreshing_ || pending_source_.has_value())) {
            set_capture_exclusion(false);
            refreshing_ = true;
            SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
            sync_control_bar();
            InvalidateRect(window_, nullptr, FALSE);
        } else {
            set_capture_exclusion(false);
            refreshing_ = false;
            refresh_surface_ = {};
            SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
            update_window_region();
            sync_control_bar();
            RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE | RDW_UPDATENOW);
        }
        refresh_was_active_on_resize_ = false;
        return 0;
    }
    case WM_LBUTTONDBLCLK:
        reset_geometry();
        return 0;
    case WM_TIMER:
        if (wparam == progress_timer) {
            progress_phase_ = std::fmod(progress_phase_ + 0.022, 1.0);
            if (window_ && IsWindowVisible(window_)) {
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        }
        if (wparam == recapture_restore_timer) {
            KillTimer(window_, recapture_restore_timer);
            if (recapturing_) {
                recapturing_ = false;
                set_capture_exclusion(false);
                SetLayeredWindowAttributes(window_, 0, 255, LWA_ALPHA);
                if (IsWindowVisible(window_)) sync_control_bar();
                InvalidateRect(window_, nullptr, FALSE);
            }
            return 0;
        }
        break;
    case WM_DPICHANGED:
        update_control_bar_region();
        sync_control_bar();
        return 0;
    case WM_CLOSE: close(); return 0;
    case WM_NCDESTROY: {
        const HWND old = window_;
        KillTimer(old, recapture_restore_timer);
        if (control_bar_) destroy_control_bar();
        SetWindowLongPtrW(old, GWLP_USERDATA, 0);
        if (!closing_) window_ = nullptr;
        return DefWindowProcW(old, message, wparam, lparam);
    }
    default: break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace minitrans
