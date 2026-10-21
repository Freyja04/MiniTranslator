#include "update_window_internal.hpp"

#include "util.hpp"

#include <shellapi.h>

#include <algorithm>
#include <cstdint>
#include <exception>
#include <memory>
#include <utility>

namespace minitrans {

using namespace update_internal;

void UpdateWindow::finish_worker() {
    if (worker_.joinable()) worker_.join();
    worker_finished_.reset();
    worker_mode_ = WorkerMode::none;
    EnableWindow(check_button_, TRUE);
}

void UpdateWindow::shutdown_worker(DWORD timeout_ms) noexcept {
    if (!worker_.joinable()) {
        worker_finished_.reset();
        worker_mode_ = WorkerMode::none;
        return;
    }
    worker_.request_stop();
    const ULONGLONG deadline = GetTickCount64() + timeout_ms;
    while (worker_finished_ &&
           !worker_finished_->load(std::memory_order_acquire) &&
           GetTickCount64() < deadline) {
        Sleep(10);
    }
    if (!worker_finished_ ||
        worker_finished_->load(std::memory_order_acquire)) {
        try { worker_.join(); } catch (...) {}
    } else {
        try { worker_.detach(); } catch (...) {}
    }
    worker_finished_.reset();
    worker_mode_ = WorkerMode::none;
}

void UpdateWindow::discard_worker_messages(HWND target) noexcept {
    if (!target) target = window_;
    if (!target) return;
    MSG message{};
    while (PeekMessageW(&message, target, check_completed_message,
                        check_completed_message, PM_REMOVE)) {
        delete reinterpret_cast<CheckResult*>(message.lParam);
    }
    while (PeekMessageW(&message, target, download_completed_message,
                        download_completed_message, PM_REMOVE)) {
        delete reinterpret_cast<DownloadResult*>(message.lParam);
    }
    while (PeekMessageW(&message, target, progress_message,
                        progress_message, PM_REMOVE)) {
        delete reinterpret_cast<std::pair<std::uint64_t, std::uint64_t>*>(message.lParam);
    }
    while (PeekMessageW(&message, target, install_prompt_message,
                        install_prompt_message, PM_REMOVE)) {
    }
}

void UpdateWindow::start_check() {
    if (worker_.joinable()) return;
    const auto manifest = config_.string(L"updates.manifest_url");
    const auto repository = config_.string(L"updates.repository_url");
    const auto channel = config_.string(L"updates.channel", L"stable");
    available_.reset();
    downloaded_.reset();
    pending_install_prompt_ = false;
    progress_visible_ = false;
    progress_received_ = 0;
    progress_total_ = 0;
    published_value_.clear();
    artifact_value_.clear();
    verification_value_.clear();
    ShowWindow(release_button_, SW_HIDE);
    ShowWindow(action_button_, SW_HIDE);
    check_role_ = ButtonRole::primary;
    SetWindowTextW(check_button_, L"检查更新");
    set_details_visible(false);
    if (manifest.empty() || repository.empty()) {
        set_state(ViewState::unconfigured, L"此构建未配置更新",
                  L"请使用 GitHub Release 中的正式安装版或便携版。");
        layout_controls();
        return;
    }
    const auto channel_name = channel == L"stable" ? L"稳定版" : L"预览版";
    set_state(ViewState::checking, L"正在检查更新",
              L"正在读取 GitHub 上的" + std::wstring(channel_name) + L"发布信息，请稍候。");
    SetWindowTextW(check_button_, L"检查中…");
    EnableWindow(check_button_, FALSE);
    worker_mode_ = WorkerMode::check;
    const auto dispatch = worker_dispatch_;
    worker_finished_ = std::make_shared<std::atomic_bool>(false);
    const auto worker_finished = worker_finished_;
    worker_ = std::jthread([dispatch, manifest, repository, channel,
                            worker_finished](std::stop_token stop) noexcept {
        try {
            auto result = std::make_unique<CheckResult>();
            try {
                WinrtApartment apartment(winrt::apartment_type::multi_threaded);
                result->update = check_for_update(manifest, repository, channel, stop);
            } catch (const std::exception& error) {
                result->cancelled = stop.stop_requested();
                if (!result->cancelled) result->error = utf8_to_wide(error.what());
            } catch (...) {
                result->cancelled = stop.stop_requested();
                if (!result->cancelled) result->error = L"检查更新时遇到未知错误";
            }
            auto* raw = result.release();
            if (!UpdateWindow::post_worker_message(
                    dispatch, check_completed_message,
                    reinterpret_cast<LPARAM>(raw))) {
                delete raw;
            }
        } catch (...) {
        }
        worker_finished->store(true, std::memory_order_release);
    });
    layout_controls();
    InvalidateRect(window_, nullptr, FALSE);
}

void UpdateWindow::start_download() {
    if (!available_ || worker_.joinable()) return;
    const auto repository = config_.string(L"updates.repository_url");
    const auto info = *available_;
    pending_install_prompt_ = false;
    progress_visible_ = true;
    progress_received_ = 0;
    progress_total_ = info.artifact.size;
    set_state(ViewState::downloading, L"正在下载 " + info.version,
              L"安装包下载完成后会自动核对大小和 SHA-256。");
    SetWindowTextW(action_button_, L"取消下载");
    action_role_ = ButtonRole::ghost;
    EnableWindow(check_button_, FALSE);
    EnableWindow(release_button_, FALSE);
    EnableWindow(action_button_, TRUE);
    resize_for_content();
    layout_controls();
    worker_mode_ = WorkerMode::download;
    const auto dispatch = worker_dispatch_;
    worker_finished_ = std::make_shared<std::atomic_bool>(false);
    const auto worker_finished = worker_finished_;
    worker_ = std::jthread([dispatch, repository, info,
                            worker_finished](std::stop_token stop) noexcept {
        try {
            auto result = std::make_unique<DownloadResult>();
            try {
                WinrtApartment apartment(winrt::apartment_type::multi_threaded);
                result->path = download_update(info, repository, stop,
                    [dispatch](std::uint64_t received, std::uint64_t total) {
                        auto* progress = new std::pair<std::uint64_t, std::uint64_t>{received, total};
                        if (!UpdateWindow::post_worker_message(
                                dispatch, progress_message,
                                reinterpret_cast<LPARAM>(progress))) {
                            delete progress;
                        }
                    });
            } catch (const std::exception& error) {
                result->cancelled = stop.stop_requested();
                if (!result->cancelled) result->error = utf8_to_wide(error.what());
            } catch (...) {
                result->cancelled = stop.stop_requested();
                if (!result->cancelled) result->error = L"下载更新时遇到未知错误";
            }
            auto* raw = result.release();
            if (!UpdateWindow::post_worker_message(
                    dispatch, download_completed_message,
                    reinterpret_cast<LPARAM>(raw))) {
                delete raw;
            }
        } catch (...) {
        }
        worker_finished->store(true, std::memory_order_release);
    });
}

void UpdateWindow::install() {
    if (!available_ || !downloaded_) return;
    if (!show_install_confirmation(instance_, window_, available_->version)) {
        return;
    }

    pending_install_prompt_ = false;
    set_state(ViewState::installing, L"正在启动安装程序",
              L"当前版本即将退出，请在安装向导中完成更新。");
    EnableWindow(check_button_, FALSE);
    EnableWindow(release_button_, FALSE);
    EnableWindow(action_button_, FALSE);
    SetWindowTextW(action_button_, L"正在启动…");
    RedrawWindow(window_, nullptr, nullptr,
                 RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    try {
        launch_update_helper(*downloaded_, *available_,
                             config_.string(L"updates.repository_url"), GetCurrentProcessId());
        ShowWindow(window_, SW_HIDE);
        if (quit_callback_) quit_callback_();
    } catch (const std::exception& error) {
        set_state(ViewState::error, L"无法启动安装程序", utf8_to_wide(error.what()));
        EnableWindow(check_button_, TRUE);
        EnableWindow(release_button_, TRUE);
        EnableWindow(action_button_, TRUE);
        action_role_ = ButtonRole::primary;
        SetWindowTextW(action_button_, L"重试安装");
        InvalidateRect(window_, nullptr, FALSE);
    }
}

void UpdateWindow::open_release() {
    if (!available_) return;
    const auto result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(window_, L"open", available_->release_url.c_str(), nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        set_state_message(L"系统浏览器未接受 GitHub 发布页面地址。");
    } else {
        set_state_message(L"已交给系统浏览器打开 GitHub 发布页面。");
    }
}

void UpdateWindow::close() {
    if (worker_.joinable()) worker_.request_stop();
    pending_install_prompt_ = false;
    ShowWindow(window_, SW_HIDE);
}

}  // namespace minitrans
