#include "app.hpp"

#include "resource.h"
#include "updater.hpp"
#include "util.hpp"

#include <winrt/Windows.Foundation.h>

#include <mutex>
#include <optional>
#include <thread>

namespace minitrans {

void AppHost::start_update_check() {
    // One inline update at a time: the settings page shows progress beside the
    // button and the worker reports back through the message window.
    if (settings_update_thread_.joinable() || shutting_down_) return;
    const auto manifest = config_.string(L"updates.manifest_url");
    const auto repository = config_.string(L"updates.repository_url");
    const auto channel = config_.string(L"updates.channel", L"stable");
    if (manifest.empty() || repository.empty()) {
        settings_.set_update_status(L"更新失败！", true);
        settings_.set_update_check_enabled(true);
        return;
    }
    const HWND dispatcher = window_;
    settings_update_thread_ = std::jthread(
        [dispatcher, manifest, repository, channel](std::stop_token stop) {
            using Outcome = SettingsUpdateOutcome;
            Outcome outcome = Outcome::failed;
            try {
                WinrtApartment apartment(winrt::apartment_type::multi_threaded);
                const auto update =
                    check_for_update(manifest, repository, channel, stop);
                if (!update) {
                    outcome = Outcome::no_update;
                } else {
                    PostMessageW(dispatcher, settings_update_stage_message, 0, 0);
                    const auto installer =
                        download_update(*update, repository, stop);
                    if (!launch_update_helper(installer, *update, repository,
                                              GetCurrentProcessId())) {
                        throw AppError("launch update helper");
                    }
                    outcome = Outcome::updated;
                }
            } catch (...) {
                outcome = stop.stop_requested() ? Outcome::cancelled
                                                : Outcome::failed;
            }
            PostMessageW(dispatcher, settings_update_completed_message,
                         static_cast<WPARAM>(outcome), 0);
        });
}

void AppHost::handle_settings_update_completed(WPARAM outcome) {
    if (settings_update_thread_.joinable()) settings_update_thread_.join();
    switch (static_cast<SettingsUpdateOutcome>(outcome)) {
    case SettingsUpdateOutcome::no_update:
        settings_.set_update_status(L"没有新版本！");
        settings_.set_update_check_enabled(true);
        break;
    case SettingsUpdateOutcome::updated:
        // The helper waits for this process to exit, then installs and the
        // installer's [Run] entry starts the new build again.
        settings_.set_update_status(L"更新中...");
        quit();
        return;
    case SettingsUpdateOutcome::failed:
    case SettingsUpdateOutcome::cancelled:
    default:
        settings_.set_update_status(L"更新失败！", true);
        settings_.set_update_check_enabled(true);
        break;
    }
}

void AppHost::start_silent_update_check() {
    if (silent_update_thread_.joinable() || shutting_down_) return;
    auto manifest = config_.string(L"updates.manifest_url");
    const auto repository = config_.string(L"updates.repository_url");
    const auto channel = config_.string(L"updates.channel", L"stable");
    if (manifest.empty() || repository.empty()) return;
    if (update_self_test_mode_) {
        // Exercise the complete worker/message/shutdown path without making a
        // smoke test depend on GitHub or the machine's proxy configuration.
        manifest = L"https://github.com/minitrans-self-test/invalid/"
                   L"releases/latest/download/update-manifest.json";
    }
    const HWND dispatcher = window_;
    silent_update_thread_ = std::jthread(
        [this, dispatcher, manifest, repository, channel](std::stop_token stop) {
            try {
                WinrtApartment apartment(winrt::apartment_type::multi_threaded);
                std::optional<UpdateInfo> result;
                try {
                    result = check_for_update(manifest, repository, channel, stop);
                } catch (...) {
                    result.reset();
                }
                {
                    std::lock_guard lock(silent_update_mutex_);
                    silent_update_result_ = std::move(result);
                }
                PostMessageW(dispatcher, silent_update_completed_message, 0, 0);
            } catch (...) {
                // No exception may escape a std::jthread entry point: doing so
                // invokes std::terminate and aborts the whole tray process.
                PostMessageW(dispatcher, silent_update_completed_message, 0, 0);
            }
        });
}

std::optional<UpdateInfo> AppHost::process_silent_update_check() {
    if (silent_update_thread_.joinable()) silent_update_thread_.join();
    std::lock_guard lock(silent_update_mutex_);
    return std::move(silent_update_result_);
}

void AppHost::handle_update_available(const UpdateInfo& update) {
    notify(std::wstring(MINITRANS_DISPLAY_NAME) + L" · 有新版本",
           std::wstring(MINITRANS_DISPLAY_NAME) + L" " + update.version +
               L" 已发布，打开设置窗口后点「检查更新」即可更新。");
}

void AppHost::restart() {
    try {
        const auto executable = executable_path();
        std::wstring command_line = L"\"" + executable.wstring() + L"\" --restart-wait-pid " +
                                    std::to_wstring(GetCurrentProcessId());
        STARTUPINFOW startup{};
        startup.cb = sizeof(startup);
        PROCESS_INFORMATION process{};
        if (!CreateProcessW(executable.c_str(), command_line.data(), nullptr, nullptr, FALSE, 0,
                            nullptr, nullptr, &startup, &process)) {
            throw_last_error("start replacement process");
        }
        CloseHandle(process.hThread);
        CloseHandle(process.hProcess);
        quit();
    } catch (const std::exception& error) {
        notify(MINITRANS_DISPLAY_NAME, L"重启失败：" + utf8_to_wide(error.what()));
    }
}

}  // namespace minitrans
