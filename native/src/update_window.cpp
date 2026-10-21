#include "update_window.hpp"
#include "update_window_internal.hpp"

#include "theme.hpp"
#include "ui_draw.hpp"
#include "util.hpp"
#include "version.hpp"

#include <commctrl.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <string>
#include <utility>

namespace minitrans {

using namespace update_internal;

UpdateWindow::UpdateWindow(HINSTANCE instance, ConfigStore& config)
    : instance_(instance), config_(config) {}

UpdateWindow::~UpdateWindow() {
    shutdown_worker(2500);
    invalidate_worker_dispatch();
    discard_worker_messages(window_);
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    if (brand_icon_) DestroyIcon(brand_icon_);
    for (auto** font : {&font_, &title_font_, &status_heading_font_,
                        &small_font_}) {
        if (*font) DeleteObject(*font);
    }
    if (background_) DeleteObject(background_);
    if (card_background_) DeleteObject(card_background_);
}

bool UpdateWindow::post_worker_message(
    const std::shared_ptr<WorkerDispatch>& dispatch,
    UINT message, LPARAM lparam) noexcept {
    if (!dispatch) return false;
    std::lock_guard lock(dispatch->mutex);
    return dispatch->target &&
           PostMessageW(dispatch->target, message, 0, lparam) != FALSE;
}

void UpdateWindow::invalidate_worker_dispatch() noexcept {
    const auto dispatch = worker_dispatch_;
    if (!dispatch) return;
    std::lock_guard lock(dispatch->mutex);
    dispatch->target = nullptr;
}

bool UpdateWindow::preprocess_message(MSG& message) {
    const HWND dialog = window_;
    if (!dialog || !IsWindow(dialog) || !IsWindowVisible(dialog)) return false;
    return IsDialogMessageW(dialog, &message) != FALSE;
}

void UpdateWindow::show(HWND owner) {
    if (window_) {
        refresh_repository_control();
        ShowWindow(window_, SW_RESTORE);
        SetWindowPos(window_, HWND_TOP, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SetForegroundWindow(window_);
        if (!worker_.joinable()) start_check();
        return;
    }
    owner_ = owner;
    available_.reset();
    downloaded_.reset();
    pending_install_prompt_ = false;
    details_visible_ = false;
    progress_visible_ = false;
    progress_received_ = 0;
    progress_total_ = 0;
    check_role_ = ButtonRole::primary;
    release_role_ = ButtonRole::ghost;
    action_role_ = ButtonRole::primary;
    if (config_.string(L"updates.repository_url").empty()) {
        view_state_ = ViewState::unconfigured;
        state_title_ = L"此构建未配置更新";
        state_message_ = L"请使用 GitHub Release 中的正式安装版或便携版。";
    } else {
        const auto channel = config_.string(L"updates.channel", L"stable") == L"stable"
            ? L"稳定版" : L"预览版";
        view_state_ = ViewState::idle;
        state_title_ = L"准备检查更新";
        state_message_ = L"将从 GitHub Release 获取" + std::wstring(channel) + L"发布信息。";
    }
    create_window(owner);
    const HWND window = window_;
    if (!window) throw AppError("update window creation returned no window");
    ShowWindow(window, SW_SHOW);
    SetForegroundWindow(window);
    start_check();
}

void UpdateWindow::refresh_appearance() {
    if (!window_ || !IsWindow(window_)) return;
    create_theme_resources();
    theme::apply_window_theme(window_, true, true);
    set_font(check_button_, font_);
    set_font(release_button_, font_);
    set_font(action_button_, font_);
    set_font(state_message_control_, small_font_);
    set_font(published_value_control_, font_);
    set_font(artifact_value_control_, font_);
    set_font(source_control_, small_font_);
    if (repository_tooltip_ && IsWindow(repository_tooltip_)) {
        theme::apply_control_theme(repository_tooltip_);
        SendMessageW(repository_tooltip_, TTM_SETTIPBKCOLOR,
                     static_cast<WPARAM>(color_input), 0);
        SendMessageW(repository_tooltip_, TTM_SETTIPTEXTCOLOR,
                     static_cast<WPARAM>(color_text), 0);
    }
    layout_controls();
    refresh_repository_control();
    RedrawWindow(window_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
}

void UpdateWindow::set_state(ViewState state, std::wstring title, std::wstring message) {
    view_state_ = state;
    state_title_ = std::move(title);
    state_message_ = std::move(message);
    sync_text_controls();
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void UpdateWindow::set_state_message(std::wstring message) {
    state_message_ = std::move(message);
    sync_text_controls();
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

void UpdateWindow::set_details_visible(bool visible) {
    if (details_visible_ == visible) return;
    details_visible_ = visible;
    resize_for_content();
    layout_controls();
    if (window_) InvalidateRect(window_, nullptr, FALSE);
}

LRESULT CALLBACK UpdateWindow::window_proc(HWND window, UINT message,
                                            WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<UpdateWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<UpdateWindow*>(create->lpCreateParams);
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
            try { self->window_error_ = "unknown update window error"; } catch (...) {}
        }
    }
    return message == WM_NCCREATE ? FALSE : message == WM_CREATE ? -1 : 0;
}

LRESULT UpdateWindow::handle_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        create_controls();
        return 0;
    case WM_SIZE:
        layout_controls();
        InvalidateRect(window_, nullptr, FALSE);
        return 0;
    case WM_GETMINMAXINFO: {
        const int minimum_height = details_visible_
            ? (progress_visible_ ? 372 : 350)
            : (progress_visible_ ? 272 : 250);
        RECT minimum{0, 0, px(560, dpi_), px(minimum_height, dpi_)};
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
        set_font(check_button_, font_);
        set_font(release_button_, font_);
        set_font(action_button_, font_);
        set_font(state_message_control_, small_font_);
        set_font(published_value_control_, font_);
        set_font(artifact_value_control_, font_);
        set_font(source_control_, small_font_);
        if (repository_tooltip_) {
            SendMessageW(repository_tooltip_, TTM_SETMAXTIPWIDTH, 0, px(640, dpi_));
        }
        layout_controls();
        RedrawWindow(window_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
        return 0;
    }
    case theme::changed_message:
        create_theme_resources();
        theme::apply_window_theme(window_, true, true);
        set_font(check_button_, font_);
        set_font(release_button_, font_);
        set_font(action_button_, font_);
        set_font(state_message_control_, small_font_);
        set_font(published_value_control_, font_);
        set_font(artifact_value_control_, font_);
        set_font(source_control_, small_font_);
        layout_controls();
        RedrawWindow(window_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
        return 0;
    case WM_COMMAND: {
        const int identifier = LOWORD(wparam);
        const int notification = HIWORD(wparam);
        if (identifier == IDCANCEL) {
            close();
            return 0;
        }
        if (notification != BN_CLICKED) break;
        if (identifier == id_check) {
            start_check();
            return 0;
        }
        if (identifier == id_release) {
            open_release();
            return 0;
        }
        if (identifier == id_action) {
            if (worker_mode_ == WorkerMode::download && worker_.joinable()) {
                worker_.request_stop();
                set_state(ViewState::downloading, L"正在取消下载",
                          L"正在停止网络请求并清理未完成的安装包。");
                SetWindowTextW(action_button_, L"正在取消…");
                EnableWindow(action_button_, FALSE);
            } else if (downloaded_) {
                install();
            } else {
                start_download();
            }
            return 0;
        }
        break;
    }
    case check_completed_message: {
        std::unique_ptr<CheckResult> result(reinterpret_cast<CheckResult*>(lparam));
        finish_worker();
        if (!result || result->cancelled) return 0;
        if (!result->error.empty()) {
            check_role_ = ButtonRole::primary;
            SetWindowTextW(check_button_, L"重试");
            set_state(ViewState::error, L"检查失败", result->error);
        } else if (!result->update) {
            const auto channel = config_.string(L"updates.channel", L"stable") == L"stable"
                ? L"稳定版" : L"预览版";
            check_role_ = ButtonRole::primary;
            SetWindowTextW(check_button_, L"重新检查");
            set_state(ViewState::current, L"已经是最新版",
                      L"当前安装的 " + std::wstring(native_version) +
                      L" 已是" + std::wstring(channel) + L"的最新版本。");
        } else {
            available_ = std::move(result->update);
            const auto channel = available_->channel == L"stable" ? L"稳定版" : L"预览版";
            set_state(ViewState::available, L"发现新版本 " + available_->version,
                      L"这是" + std::wstring(channel) +
                      L"。可直接下载，程序会自动校验安装包完整性。");
            published_value_ = format_published_at(available_->published_at);
            artifact_value_ = available_->artifact.name + L"（" +
                              format_size(available_->artifact.size) + L"）";
            verification_value_ = L"下载完成后自动校验 SHA-256";
            check_role_ = ButtonRole::ghost;
            action_role_ = ButtonRole::primary;
            SetWindowTextW(check_button_, L"重新检查");
            SetWindowTextW(action_button_, L"下载并安装");
            ShowWindow(release_button_, SW_SHOW);
            ShowWindow(action_button_, SW_SHOW);
            set_details_visible(true);
            if (update_available_callback_) update_available_callback_(*available_);
        }
        layout_controls();
        RedrawWindow(window_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return 0;
    }
    case progress_message: {
        std::unique_ptr<std::pair<std::uint64_t, std::uint64_t>> value(
            reinterpret_cast<std::pair<std::uint64_t, std::uint64_t>*>(lparam));
        if (value && value->second) {
            progress_total_ = value->second;
            progress_received_ = std::min(value->first, value->second);
            const int percent = static_cast<int>(std::min<std::uint64_t>(
                100, progress_received_ * 100 / progress_total_));
            set_state_message(
                L"已下载 " + format_size(progress_received_) + L" / " +
                format_size(progress_total_) + L"（" + std::to_wstring(percent) +
                L"%），完成后自动校验大小和 SHA-256。");
        }
        return 0;
    }
    case download_completed_message: {
        std::unique_ptr<DownloadResult> result(reinterpret_cast<DownloadResult*>(lparam));
        finish_worker();
        EnableWindow(action_button_, TRUE);
        EnableWindow(release_button_, TRUE);
        EnableWindow(check_button_, TRUE);
        if (!result) return 0;
        if (result->cancelled) {
            progress_visible_ = false;
            set_state(ViewState::available, L"下载已取消",
                      L"没有安装任何文件，可以随时重新下载。");
            action_role_ = ButtonRole::primary;
            SetWindowTextW(action_button_, L"下载并安装");
            resize_for_content();
        } else if (!result->error.empty()) {
            progress_visible_ = false;
            downloaded_.reset();
            set_state(ViewState::error, L"下载失败", result->error);
            action_role_ = ButtonRole::primary;
            SetWindowTextW(action_button_, L"重试下载");
            resize_for_content();
        } else {
            downloaded_ = std::move(result->path);
            progress_received_ = progress_total_;
            set_state(ViewState::ready, L"安装包已验证",
                      L"文件大小和 SHA-256 均与更新清单一致，可以开始安装。");
            verification_value_ = L"大小和 SHA-256 校验已通过";
            action_role_ = ButtonRole::primary;
            SetWindowTextW(action_button_, L"安装更新");
            pending_install_prompt_ = true;
            PostMessageW(window_, install_prompt_message, 0, 0);
        }
        layout_controls();
        RedrawWindow(window_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        return 0;
    }
    case install_prompt_message:
        if (pending_install_prompt_ && downloaded_ && available_) {
            pending_install_prompt_ = false;
            install();
        }
        return 0;
    case WM_DRAWITEM:
        if (const auto* item = reinterpret_cast<DRAWITEMSTRUCT*>(lparam)) {
            return draw_item(*item);
        }
        return FALSE;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC: {
        const HWND control = reinterpret_cast<HWND>(lparam);
        if (control == state_message_control_ ||
            control == published_value_control_ ||
            control == artifact_value_control_ ||
            control == source_control_) {
            HDC dc = reinterpret_cast<HDC>(wparam);
            const bool source = control == source_control_;
            const bool dim = source || control == state_message_control_;
            SetTextColor(dc, dim ? color_text_dim : color_text);
            SetBkColor(dc, source ? color_page : color_card);
            SetBkMode(dc, OPAQUE);
            return reinterpret_cast<LRESULT>(source ? background_ : card_background_);
        }
        break;
    }
    case WM_PAINT:
        paint();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_KEYDOWN:
        if (wparam == VK_ESCAPE) {
            close();
            return 0;
        }
        break;
    case WM_CLOSE:
        close();
        return 0;
    case WM_NCDESTROY: {
        const HWND destroyed = window_;
        pending_install_prompt_ = false;
        if (worker_.joinable()) worker_.request_stop();
        invalidate_worker_dispatch();
        discard_worker_messages(destroyed);
        SetWindowLongPtrW(destroyed, GWLP_USERDATA, 0);
        window_ = nullptr;
        check_button_ = nullptr;
        release_button_ = nullptr;
        action_button_ = nullptr;
        state_message_control_ = nullptr;
        published_value_control_ = nullptr;
        artifact_value_control_ = nullptr;
        source_control_ = nullptr;
        repository_tooltip_ = nullptr;
        worker_dispatch_.reset();
        return DefWindowProcW(destroyed, message, wparam, lparam);
    }
    default:
        break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace minitrans
