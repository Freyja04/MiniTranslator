#include "app.hpp"

#include "block_editor.hpp"
#include "hotkey.hpp"
#include "language.hpp"
#include "resource.h"
#include "theme.hpp"
#include "tray_menu.hpp"
#include "ui_icons.hpp"
#include "util.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <shellapi.h>
#include <shellscalingapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <cwchar>
#include <filesystem>
#include <vector>

namespace minitrans {

namespace {

constexpr UINT tray_message = WM_APP + 2;
constexpr UINT initialize_message = WM_APP + 3;
constexpr UINT_PTR capture_timer = 1;
constexpr UINT_PTR recapture_timer = 2;
constexpr UINT_PTR update_timer = 3;
constexpr UINT_PTR text_translate_timer = 4;
constexpr UINT_PTR copy_toast_timer = 5;
constexpr int hotkey_capture = 1;
constexpr int hotkey_toggle = 2;
constexpr int hotkey_text_translate = 3;
constexpr UINT command_capture = 3001;
constexpr UINT command_restore = 3002;
constexpr UINT command_settings = 3003;
constexpr UINT command_exit = 3006;
constexpr UINT command_text_translate = 3007;
constexpr UINT tray_icon_id = 1;

}  // namespace

AppHost::AppHost(HINSTANCE instance, std::filesystem::path config_path)
    : instance_(instance),
      config_(config_path.empty() ? app_data_directory() / L"config.json"
                                  : std::move(config_path)),
      settings_(instance, config_),
      overlay_(instance), result_(instance) {
    theme::initialize(config_);
    create_message_window();
    pipeline_ = std::make_unique<PipelineController>(window_);
    text_session_ = std::make_unique<TextTranslationSession>(config_, *pipeline_);
    text_session_->set_source(config_.string(L"text.source", L"zh-Hans"));
    text_session_->set_target(config_.string(L"text.target", L"en"));
    quick_window_ = std::make_unique<QuickTranslateWindow>(instance_, *text_session_);
    TextTranslationCallbacks text_callbacks;
    text_callbacks.input_changed = [this](std::wstring value) {
        on_text_input_changed(std::move(value));
    };
    text_callbacks.source_changed = [this](std::wstring value) {
        on_text_source_changed(std::move(value));
    };
    text_callbacks.target_changed = [this](std::wstring value) {
        on_text_target_changed(std::move(value));
    };
    text_callbacks.composition_changed = [this](bool composing) {
        on_text_composition_changed(composing);
    };
    text_callbacks.translate_now = [this] { schedule_text_translation(true); };
    text_callbacks.clear = [this] {
        KillTimer(window_, text_translate_timer);
        if (pipeline_) pipeline_->cancel_lane(PipelineLane::text);
        if (text_session_) text_session_->clear();
    };
    text_callbacks.copy = [this] {
        if (!text_session_ || text_session_->output().empty()) return;
        copy_to_clipboard(text_session_->output());
        text_session_->note_copied();
        refresh_text_translation_views();
        SetTimer(window_, copy_toast_timer,
                 static_cast<UINT>(
                     std::chrono::duration_cast<std::chrono::milliseconds>(
                         TextTranslationSession::copy_confirmation_duration)
                         .count()),
                 nullptr);
    };
    text_callbacks.copy_source = [this] {
        if (text_session_) copy_to_clipboard(text_session_->input());
    };
    text_callbacks.clear_output = [this] {
        if (text_session_) text_session_->clear_output();
    };
    text_callbacks.capture = [this] { start_capture(); };
    text_callbacks.open_settings = [this] { open_translation_settings(); };
    text_callbacks.home = [this] {
        if (quick_window_) quick_window_->hide();
        open_settings();
    };
    text_callbacks.hidden = [this] {
        // The reply can no longer be shown, so stop the debounce and any request
        // still on the wire. A finished translation is kept: reopening the
        // window should show it again.
        KillTimer(window_, text_translate_timer);
        if (pipeline_) pipeline_->cancel_lane(PipelineLane::text);
        if (text_session_ &&
            text_session_->state() == TextTranslationState::translating) {
            text_session_->cancel();
        }
    };
    settings_.attach_text_translation(*text_session_, text_callbacks);
    settings_.set_capture_callback([this] { start_capture_from_settings(); });
    quick_window_->set_callbacks(std::move(text_callbacks));
    text_session_->set_changed_callback([this] { refresh_text_translation_views(); });
    result_.set_copy_callback([this](std::wstring_view text) { copy_to_clipboard(text); });
    result_.set_retry_callback([this] { retry_translation(); });
    result_.set_edit_callback([this] { open_block_editor(); });
    result_.set_closed_callback([this] {
        BlockEditor::close_active();
        KillTimer(window_, recapture_timer);
        pending_recapture_.reset();
        current_request_ = 0;
        block_translation_request_.reset();
        last_image_.reset();
        last_blocks_.clear();
        last_result_.reset();
        if (pipeline_) pipeline_->cancel_lane(PipelineLane::visual);
    });
    result_.set_recapture_callback([this](const RECT& rect) { request_recapture(rect); });
    settings_.set_hotkeys_changed_callback([this] { return apply_hotkeys(true); });
    settings_.set_restart_callback([this] { restart(); });
    settings_.set_update_check_callback([this] { start_update_check(); });
    settings_.set_settings_changed_callback([this] {
        refresh_pipeline_options();
        refresh_theme_windows();
        refresh_tray_tooltip();
        if (text_session_ && text_session_->configuration_changed()) {
            schedule_text_translation(true);
        }
    });
    refresh_pipeline_options();
    if (!PostMessageW(window_, initialize_message, 0, 0)) {
        throw_last_error("queue application initialization");
    }
}

AppHost::~AppHost() {
    shutting_down_ = true;
    if (settings_update_thread_.joinable()) {
        settings_update_thread_.request_stop();
        settings_update_thread_.join();
    }
    BlockEditor::close_active();
    result_.set_closed_callback({});
    result_.close();
    overlay_.close();
    KillTimer(window_, text_translate_timer);
    if (text_session_) text_session_->set_changed_callback({});
    settings_.detach_text_translation();
    quick_window_.reset();
    text_session_.reset();
    pipeline_.reset();
    if (silent_update_thread_.joinable()) {
        silent_update_thread_.request_stop();
        silent_update_thread_.join();
    }
    remove_tray_icon();
    if (window_) {
        UnregisterHotKey(window_, hotkey_capture);
        UnregisterHotKey(window_, hotkey_toggle);
        UnregisterHotKey(window_, hotkey_text_translate);
        DestroyWindow(window_);
        window_ = nullptr;
    }
}

void AppHost::create_message_window() {
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = &AppHost::window_proc;
    description.hInstance = instance_;
    description.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    description.lpszClassName = window_class_name();
    if (!RegisterClassExW(&description) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw_last_error("register application window");
    }
    window_ = CreateWindowExW(0, window_class_name(), MINITRANS_DISPLAY_NAME, 0,
                              0, 0, 0, 0, nullptr, nullptr, instance_, this);
    if (!window_) throw_last_error("create application window");
    taskbar_created_message_ = RegisterWindowMessageW(L"TaskbarCreated");
}

void AppHost::add_tray_icon() {
    if (icon_) {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }

    HICON icon = tray_menu::load_app_icon(instance_);
    if (!icon) throw_last_error("load notification icon");

    NOTIFYICONDATAW tray{};
    tray.cbSize = sizeof(tray);
    tray.hWnd = window_;
    tray.uID = tray_icon_id;
    tray.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
    tray.uCallbackMessage = tray_message;
    tray.hIcon = icon;
    const auto capture = config_.string(L"hotkey", L"Ctrl+Alt+Q");
    const auto toggle = config_.string(L"hotkey_toggle", L"Ctrl+Alt+W");
    const auto text = config_.string(L"hotkey_text_translate", L"Ctrl+Alt+Space");
    const auto tip = std::wstring(MINITRANS_DISPLAY_NAME) +
                     L"\n截图翻译：" + capture + L"\n快速翻译：" + text +
                     L"\n收起 / 显示：" + toggle;
    wcsncpy_s(tray.szTip, tip.c_str(), _TRUNCATE);
    if (!Shell_NotifyIconW(NIM_ADD, &tray)) {
        DestroyIcon(icon);
        throw AppError("cannot create notification icon");
    }
    tray.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &tray);
    tray_ = tray;
    icon_ = icon;
}

void AppHost::remove_tray_icon() {
    if (tray_.hWnd) {
        Shell_NotifyIconW(NIM_DELETE, &tray_);
        tray_.hWnd = nullptr;
    }
    if (icon_) {
        DestroyIcon(icon_);
        icon_ = nullptr;
    }
}

void AppHost::refresh_tray_icon() {
    if (!tray_.hWnd || icon_) return;
    HICON replacement = tray_menu::load_app_icon(instance_);
    if (!replacement) return;
    const HICON previous = icon_;
    tray_.uFlags = NIF_ICON;
    tray_.hIcon = replacement;
    if (Shell_NotifyIconW(NIM_MODIFY, &tray_)) {
        icon_ = replacement;
        if (previous) DestroyIcon(previous);
    } else {
        tray_.hIcon = previous;
        DestroyIcon(replacement);
    }
}

void AppHost::refresh_tray_tooltip() {
    if (!tray_.hWnd) return;
    const auto capture = config_.string(L"hotkey", L"Ctrl+Alt+Q");
    const auto toggle = config_.string(L"hotkey_toggle", L"Ctrl+Alt+W");
    const auto text = config_.string(L"hotkey_text_translate", L"Ctrl+Alt+Space");
    const auto tip = std::wstring(MINITRANS_DISPLAY_NAME) +
                     L"\n截图翻译：" + capture + L"\n快速翻译：" + text +
                     L"\n收起 / 显示：" + toggle;
    tray_.uFlags = NIF_TIP;
    wcsncpy_s(tray_.szTip, tip.c_str(), _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &tray_);
}

void AppHost::refresh_theme_windows() {
    settings_.refresh_appearance();
    if (quick_window_) quick_window_->refresh_appearance();
    if (result_.handle()) {
        result_.refresh_appearance(result_appearance());
        PostMessageW(result_.handle(), theme::changed_message, 0, 0);
    }
    BlockEditor::refresh_active_theme();
    refresh_tray_icon();
    refresh_tray_tooltip();
    if (window_) RedrawWindow(window_, nullptr, nullptr, RDW_INVALIDATE);
}

void AppHost::notify(std::wstring_view title, std::wstring_view message) {
    if (!tray_.hWnd) return;
    tray_.uFlags = NIF_INFO;
    wcsncpy_s(tray_.szInfoTitle, std::wstring(title).c_str(), _TRUNCATE);
    wcsncpy_s(tray_.szInfo, std::wstring(message).c_str(), _TRUNCATE);
    tray_.dwInfoFlags = NIIF_USER;
    tray_.hBalloonIcon = icon_;
    tray_.uTimeout = 3000;
    Shell_NotifyIconW(NIM_MODIFY, &tray_);
}

void AppHost::show_tray_menu(POINT location) {
    HMENU menu = CreatePopupMenu();
    if (!menu) return;

    const UINT dpi = tray_menu::menu_dpi(location);
    HFONT menu_font = tray_menu::create_font(dpi);
    const bool owns_font = menu_font != nullptr;
    if (!menu_font) menu_font = static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    HBRUSH menu_background = CreateSolidBrush(theme::tray_color_background);
    MENUINFO menu_info{};
    menu_info.cbSize = sizeof(menu_info);
    menu_info.fMask = MIM_STYLE | (menu_background ? MIM_BACKGROUND : 0);
    menu_info.dwStyle = MNS_NOCHECK;
    menu_info.hbrBack = menu_background;
    SetMenuInfo(menu, &menu_info);

    const auto capture = config_.string(L"hotkey", L"Ctrl+Alt+Q");
    const auto toggle = config_.string(L"hotkey_toggle", L"Ctrl+Alt+W");
    const auto text = config_.string(L"hotkey_text_translate", L"Ctrl+Alt+Space");
    const bool can_restore = last_image_ && !result_.visible();
    std::vector<tray_menu::Item> items;
    items.reserve(7);
    const auto add_item = [&](UINT command, tray_menu::Icon icon, std::wstring label,
                              std::wstring secondary = {}, bool enabled = true) {
        tray_menu::Item item;
        item.command = command;
        item.icon = icon;
        item.label = std::move(label);
        item.secondary = std::move(secondary);
        item.dpi = dpi;
        item.font = menu_font;
        item.enabled = enabled;
        items.push_back(std::move(item));
    };
    add_item(command_text_translate, tray_menu::Icon::brand, L"文本翻译", text);
    add_item(command_capture, tray_menu::Icon::capture, L"截图翻译", capture);
    add_item(command_restore, tray_menu::Icon::restore, L"显示上次译文", toggle, can_restore);
    tray_menu::Item separator;
    separator.separator = true;
    separator.enabled = false;
    separator.dpi = dpi;
    separator.font = menu_font;
    items.push_back(std::move(separator));
    add_item(command_settings, tray_menu::Icon::home, L"首页");
    add_item(command_exit, tray_menu::Icon::exit, L"退出");
    items.front().first = true;
    items.back().last = true;

    for (std::size_t index = 0; index < items.size(); ++index) {
        auto& item = items[index];
        item.accessible_text = item.label;
        if (!item.secondary.empty()) item.accessible_text += L"\t" + item.secondary;

        MENUITEMINFOW menu_item{};
        menu_item.cbSize = sizeof(menu_item);
        menu_item.fMask = MIIM_FTYPE | MIIM_STATE | MIIM_ID | MIIM_DATA | MIIM_STRING;
        menu_item.fType = MFT_OWNERDRAW | (item.separator ? MFT_SEPARATOR : 0);
        menu_item.fState = item.enabled ? MFS_ENABLED : MFS_DISABLED;
        menu_item.wID = item.command;
        menu_item.dwItemData = reinterpret_cast<ULONG_PTR>(&item);
        menu_item.dwTypeData = item.accessible_text.data();
        menu_item.cch = static_cast<UINT>(item.accessible_text.size());
        InsertMenuItemW(menu, static_cast<UINT>(index), TRUE, &menu_item);
    }

    SetForegroundWindow(window_);
    HWINEVENTHOOK menu_hook = SetWinEventHook(
        EVENT_SYSTEM_MENUPOPUPSTART, EVENT_SYSTEM_MENUPOPUPSTART, nullptr,
        &tray_menu::style_menu_window, GetCurrentProcessId(), GetCurrentThreadId(),
        WINEVENT_OUTOFCONTEXT);
    TrackPopupMenu(menu, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN | TPM_LEFTALIGN,
                   location.x, location.y, 0, window_, nullptr);
    if (menu_hook) UnhookWinEvent(menu_hook);
    DestroyMenu(menu);
    if (menu_background) DeleteObject(menu_background);
    if (owns_font) DeleteObject(menu_font);
    PostMessageW(window_, WM_NULL, 0, 0);
}

bool AppHost::apply_hotkeys(bool announce_changes) {
    // Release all registrations first so exchanging shortcuts is valid.
    const auto unregister_all = [this] {
        UnregisterHotKey(window_, hotkey_capture);
        UnregisterHotKey(window_, hotkey_toggle);
        UnregisterHotKey(window_, hotkey_text_translate);
    };
    unregister_all();
    bool success = true;
    std::wstring error;
    const auto capture_spec = config_.string(L"hotkey", L"Ctrl+Alt+Q");
    const auto toggle_spec = config_.string(L"hotkey_toggle", L"Ctrl+Alt+W");
    const auto text_spec = config_.string(
        L"hotkey_text_translate", L"Ctrl+Alt+Space");
    const bool capture_registered = register_hotkey(
        window_, hotkey_capture, capture_spec, &error);
    if (!capture_registered) {
        success = false;
        notify(std::wstring(MINITRANS_DISPLAY_NAME) + L" · 截图翻译快捷键未生效", error);
    }
    error.clear();
    const bool toggle_registered = register_hotkey(
        window_, hotkey_toggle, toggle_spec, &error);
    if (!toggle_registered) {
        success = false;
        notify(std::wstring(MINITRANS_DISPLAY_NAME) + L" · 收起 / 显示快捷键未生效", error);
    }
    error.clear();
    const bool text_registered = register_hotkey(
        window_, hotkey_text_translate, text_spec, &error);
    if (!text_registered) {
        success = false;
        notify(std::wstring(MINITRANS_DISPLAY_NAME) + L" · 快速翻译快捷键未生效", error);
    }
    if (!success) {
        unregister_all();
        bool restored = true;
        const auto restore = [&](int id, const std::wstring& spec) {
            if (spec.empty()) return;
            std::wstring restore_error;
            if (!register_hotkey(window_, id, spec, &restore_error)) restored = false;
        };
        restore(hotkey_capture, registered_capture_hotkey_);
        restore(hotkey_toggle, registered_toggle_hotkey_);
        restore(hotkey_text_translate, registered_text_hotkey_);
        if (!restored) {
            notify(std::wstring(MINITRANS_DISPLAY_NAME) + L" · 快捷键恢复失败",
                   L"原快捷键也被其他程序占用，请在设置中重新选择");
        }
        return false;
    }
    {
        const auto old_capture = registered_capture_hotkey_;
        const auto old_toggle = registered_toggle_hotkey_;
        const auto old_text = registered_text_hotkey_;
        registered_capture_hotkey_ = capture_spec;
        registered_toggle_hotkey_ = toggle_spec;
        registered_text_hotkey_ = text_spec;
        refresh_tray_tooltip();
        if (announce_changes && !old_capture.empty() && old_capture != capture_spec) {
            notify(MINITRANS_DISPLAY_NAME, L"截图翻译快捷键已改为 " + capture_spec);
        }
        if (announce_changes && !old_toggle.empty() && old_toggle != toggle_spec) {
            notify(MINITRANS_DISPLAY_NAME, L"收起 / 显示快捷键已改为 " + toggle_spec);
        }
        if (announce_changes && !old_text.empty() && old_text != text_spec) {
            notify(MINITRANS_DISPLAY_NAME, L"快速翻译快捷键已改为 " + text_spec);
        }
    }
    return true;
}

void AppHost::start_capture() {
    if (overlay_.active() || shutting_down_) return;
    if (quick_window_) quick_window_->hide();
    BlockEditor::close_active();
    KillTimer(window_, recapture_timer);
    pending_recapture_.reset();
    if (pipeline_) pipeline_->cancel_lane(PipelineLane::visual);
    current_request_ = 0;
    block_translation_request_.reset();
    result_.close();
    last_image_.reset();
    last_blocks_.clear();
    last_result_.reset();
    KillTimer(window_, capture_timer);
    SetTimer(window_, capture_timer, 30, nullptr);
}

void AppHost::start_capture_from_settings() {
    if (overlay_.active() || shutting_down_) return;
    // Hide the settings window before the overlay freezes the desktop so the
    // panel itself does not end up in the frozen bitmap, then bring it back once
    // the selection finishes or is cancelled.
    settings_.hide_for_capture();
    settings_hidden_for_capture_ = true;
    start_capture();
}

void AppHost::begin_capture_now() {
    const bool restore_settings = settings_hidden_for_capture_;
    settings_hidden_for_capture_ = false;
    try {
        overlay_.start(theme::accent,
            [this, restore_settings](const RECT& rect, PixelBuffer image) noexcept {
                try {
                    on_selected(rect, std::move(image));
                } catch (const std::exception& error) {
                    notify(MINITRANS_DISPLAY_NAME, L"无法创建译文窗口：" + utf8_to_wide(error.what()));
                } catch (...) {
                    notify(MINITRANS_DISPLAY_NAME, L"无法创建译文窗口：未知错误");
                }
                if (restore_settings) settings_.show_after_capture();
            },
            [this, restore_settings] {
                if (restore_settings) settings_.show_after_capture();
            });
    } catch (const std::exception& error) {
        if (restore_settings) settings_.show_after_capture();
        notify(MINITRANS_DISPLAY_NAME, L"无法截取屏幕：" + utf8_to_wide(error.what()));
    }
}

void AppHost::on_selected(const RECT& rect, PixelBuffer image) {
    auto shared = std::make_shared<PixelBuffer>(std::move(image));
    start_pipeline(shared, rect);
    // Commit the replacement surface before SelectionOverlay destroys the
    // frozen desktop. The pixels under the selected area stay identical.
    DwmFlush();
}

void AppHost::refresh_pipeline_options() {
    pipeline_options_ = PipelineOptions::from_config(config_);
    pipeline_options_ready_ = true;
}

const PipelineOptions& AppHost::pipeline_options() {
    if (!pipeline_options_ready_) refresh_pipeline_options();
    return pipeline_options_;
}

void AppHost::start_pipeline(std::shared_ptr<const PixelBuffer> image, const RECT& rect) {
    last_image_ = image;
    result_.show_loading(rect, image, result_appearance());
    try {
        current_request_ = pipeline_->schedule(image, pipeline_options());
    } catch (const std::exception& error) {
        result_.set_error(L"启动翻译失败：" + utf8_to_wide(error.what()));
    }
}

void AppHost::process_pipeline_completions() {
    if (!pipeline_) return;
    for (auto& completion : pipeline_->take_completions()) {
        if (completion.lane == PipelineLane::text) {
            if (text_session_) text_session_->handle_completion(std::move(completion));
            continue;
        }
        if (block_translation_request_ &&
            completion.request_id == block_translation_request_->request_id) {
            process_block_translation(std::move(completion));
            continue;
        }
        if (!current_request_ || completion.request_id != current_request_) continue;
        if (completion.result) {
            const auto auto_copy = config_.boolean(L"appearance.auto_copy", true);
            const auto copy = completion.result->plain_text;
            last_result_ = *completion.result;
            last_blocks_.clear();
            last_blocks_.reserve(completion.result->blocks.size());
            for (const auto& item : completion.result->blocks) {
                last_blocks_.push_back(item.block);
            }
            result_.set_result(std::move(*completion.result));
            if (auto_copy && !copy.empty()) copy_to_clipboard(copy);
        } else {
            result_.set_error(completion.error.empty() ? L"识别或翻译失败" : completion.error);
        }
    }
}

void AppHost::open_text_translation() {
    if (shutting_down_) return;
    try {
        settings_.show_text_translation(window_);
    } catch (const std::exception& error) {
        notify(MINITRANS_DISPLAY_NAME, L"无法打开文字翻译：" + utf8_to_wide(error.what()));
    }
}

void AppHost::toggle_quick_translation() {
    if (!quick_window_ || shutting_down_ || overlay_.active()) return;
    // Opening the window is a natural moment to resolve the provider and the
    // language pair, well before the debounce timer submits the text.
    if (text_session_) text_session_->prewarm();
    try {
        quick_window_->toggle_near(window_, tray_menu::anchor_for_quick_translation(window_, tray_icon_id), true);
    } catch (const std::exception& error) {
        notify(MINITRANS_DISPLAY_NAME, L"无法打开快速翻译：" + utf8_to_wide(error.what()));
    }
}

void AppHost::on_text_input_changed(std::wstring value) {
    if (!text_session_ || !text_session_->set_input(std::move(value))) return;
    schedule_text_translation(false);
}

void AppHost::on_text_source_changed(std::wstring value) {
    if (!text_session_ || !text_session_->set_source(std::move(value))) return;
    config_.set_string(L"text.source", text_session_->source());
    config_.save();
    schedule_text_translation(true);
}

void AppHost::on_text_target_changed(std::wstring value) {
    if (!text_session_ || !text_session_->set_target(std::move(value))) return;
    config_.set_string(L"text.target", text_session_->target());
    config_.save();
    schedule_text_translation(true);
}

void AppHost::on_text_composition_changed(bool composing) {
    text_composing_ = composing;
    KillTimer(window_, text_translate_timer);
    if (!composing) schedule_text_translation(false);
}

void AppHost::schedule_text_translation(bool immediate) {
    KillTimer(window_, text_translate_timer);
    if (!text_session_ || text_composing_ || !text_session_->can_submit()) {
        // Nothing new can be submitted (empty, too long, or composing), so any
        // reply still on the wire would land in a view that no longer matches
        // the input. Drop it instead of letting it finish in the background.
        if (pipeline_) pipeline_->cancel_lane(PipelineLane::text);
        return;
    }
    if (immediate) {
        text_session_->submit();
    } else {
        SetTimer(window_, text_translate_timer, 500, nullptr);
    }
}

void AppHost::refresh_text_translation_views() {
    settings_.refresh_text_translation();
    if (quick_window_) quick_window_->refresh();
}

void AppHost::toggle_result() {
    if (result_.visible()) {
        result_.minimize();
        const auto key = config_.string(L"hotkey_toggle", L"Ctrl+Alt+W");
        notify(MINITRANS_DISPLAY_NAME,
               L"译文已收起，按 " + key + L" 或从托盘菜单「显示上次译文」叫回来");
    } else if (last_image_) {
        result_.restore();
    } else {
        notify(MINITRANS_DISPLAY_NAME, L"现在没有译文可以显示");
    }
}

void AppHost::copy_to_clipboard(std::wstring_view text) {
    copy_text_to_clipboard(window_, text);
}

void AppHost::retry_translation() {
    if (!last_image_) return;
    BlockEditor::close_active();
    if (pipeline_) pipeline_->cancel_lane(PipelineLane::visual);
    block_translation_request_.reset();
    result_.show_retry_loading(result_appearance());
    current_request_ = 0;
    try {
        if (!last_blocks_.empty()) {
            current_request_ = pipeline_->schedule_translation(last_blocks_,
                                                               pipeline_options());
        } else {
            current_request_ = pipeline_->schedule(last_image_, pipeline_options());
        }
    } catch (const std::exception& error) {
        result_.set_error(L"\u542F\u52A8\u7FFB\u8BD1\u5931\u8D25\uFF1A" +
                          utf8_to_wide(error.what()));
    }
}

void AppHost::open_block_editor() {
    if (!last_result_ || last_result_->blocks.empty() || !result_.handle()) return;
    if (block_translation_request_) {
        notify(MINITRANS_DISPLAY_NAME, L"文本块仍在重译，请稍后再打开校对窗口");
        return;
    }
    result_.set_editing(true);
    try {
        const bool opened = BlockEditor::show(instance_, result_.handle(), *last_result_,
            chinese_target_for_selection(config_.string(L"lang.target", L"zh-Hans")),
            [this](BlockEditor& editor, BlockEditor::Change change) {
                request_block_translation(editor, change.index, std::move(change.block));
            },
            [this](BlockEditor& editor) { cancel_block_translation(editor); },
            [this](std::optional<std::vector<BlockEditor::Change>> edited) {
                if (!edited) {
                    if (!block_translation_request_) result_.set_editing(false);
                    return;
                }
                std::vector<std::size_t> indices;
                std::vector<TextBlock> blocks;
                indices.reserve(edited->size());
                blocks.reserve(edited->size());
                for (auto& change : *edited) {
                    indices.push_back(change.index);
                    blocks.push_back(std::move(change.block));
                }
                if (blocks.empty() ||
                    !schedule_block_translations(std::move(indices),
                                                 std::move(blocks), nullptr)) {
                    result_.set_editing(false);
                }
            });
        if (!opened) {
            result_.set_editing(false);
        }
    } catch (const std::exception& error) {
        result_.set_editing(false);
        notify(MINITRANS_DISPLAY_NAME, L"无法打开校对窗口：" + utf8_to_wide(error.what()));
    }
}

void AppHost::request_block_translation(BlockEditor& editor, std::size_t index,
                                        TextBlock block) {
    if (!last_result_ || index >= last_result_->blocks.size() || !result_.handle()) {
        editor.set_error(index, L"当前译文已经失效，请重新框选");
        return;
    }
    schedule_block_translations({index}, {std::move(block)}, &editor);
}

void AppHost::cancel_block_translation(BlockEditor& editor) noexcept {
    if (!block_translation_request_ || block_translation_request_->editor != &editor) return;
    // Closing the non-modal editor detaches its UI, but the submitted block
    // remains valid and should still update the result when it completes.
    block_translation_request_->editor = nullptr;
}

bool AppHost::schedule_block_translations(std::vector<std::size_t> indices,
                                          std::vector<TextBlock> blocks,
                                          BlockEditor* editor) {
    if (!pipeline_ || indices.empty() || indices.size() != blocks.size()) return false;
    for (const auto index : indices) {
        if (!last_result_ || index >= last_result_->blocks.size()) {
            if (editor) editor->set_error(index, L"当前译文已经失效，请重新框选");
            return false;
        }
    }

    std::uint64_t request_id{};
    try {
        // Re-translation shares the visual lane, so an in-flight screenshot request
        // is cancelled on purpose: its result would overwrite the blocks this
        // request is about to replace.
        current_request_ = 0;
        request_id = pipeline_->schedule_translation(std::move(blocks),
                                                     pipeline_options());
    } catch (const std::exception& error) {
        auto replaced = std::move(block_translation_request_);
        block_translation_request_.reset();
        pipeline_->cancel_lane(PipelineLane::visual);
        if (replaced && replaced->editor) {
            for (const auto index : replaced->indices) {
                replaced->editor->set_error(
                    index, L"已由新的重译请求替换，请重试");
            }
        }
        const auto message = L"启动单块重译失败：" + utf8_to_wide(error.what());
        if (editor) {
            for (const auto index : indices) editor->set_error(index, message);
        } else {
            notify(std::wstring(MINITRANS_DISPLAY_NAME) + L" · 单块重译失败", message);
        }
        return false;
    }

    auto replaced = std::move(block_translation_request_);
    block_translation_request_ = BlockTranslationRequest{
        request_id, std::move(indices), editor,
    };
    if (replaced && replaced->editor) {
        for (const auto index : replaced->indices) {
            replaced->editor->set_error(index, L"已由新的重译请求替换，请重试");
        }
    }
    return true;
}

void AppHost::process_block_translation(PipelineCompletion completion) {
    if (!block_translation_request_ ||
        completion.request_id != block_translation_request_->request_id) {
        return;
    }
    auto request = std::move(*block_translation_request_);
    block_translation_request_.reset();

    const auto fail = [&](std::wstring message) {
        if (request.editor) {
            for (const auto index : request.indices) {
                request.editor->set_error(index, message);
            }
        } else {
            result_.set_editing(false);
            notify(std::wstring(MINITRANS_DISPLAY_NAME) + L" · 单块重译失败", message);
        }
    };

    if (!completion.result) {
        fail(completion.error.empty() ? L"单块重译失败" : std::move(completion.error));
        return;
    }
    if (!last_result_ || completion.result->blocks.size() != request.indices.size() ||
        last_result_->blocks.size() != last_blocks_.size()) {
        fail(L"单块重译结果与当前文本块不一致，请重新打开校对窗口");
        return;
    }
    for (const auto index : request.indices) {
        if (index >= last_result_->blocks.size()) {
            fail(L"一个文本块已经失效，请重新打开校对窗口");
            return;
        }
    }

    for (std::size_t offset = 0; offset < request.indices.size(); ++offset) {
        const auto index = request.indices[offset];
        const auto& translated = completion.result->blocks[offset];
        last_result_->blocks[index] = translated;
        last_blocks_[index] = translated.block;
        if (request.editor) request.editor->set_translation(index, translated);
    }
    refresh_cached_result();
    if (!request.editor) result_.set_editing(false);
}

void AppHost::refresh_cached_result() {
    if (!last_result_) return;
    last_result_->plain_text.clear();
    for (const auto& block : last_result_->blocks) {
        if (block.translated.empty()) continue;
        if (!last_result_->plain_text.empty()) last_result_->plain_text.push_back(L'\n');
        last_result_->plain_text += block.translated;
    }
    result_.set_result(*last_result_);
    if (config_.boolean(L"appearance.auto_copy", true) &&
        !last_result_->plain_text.empty()) {
        copy_to_clipboard(last_result_->plain_text);
    }
}

void AppHost::request_recapture(const RECT& rect) {
    BlockEditor::close_active();
    if (pipeline_) pipeline_->cancel_lane(PipelineLane::visual);
    current_request_ = 0;
    block_translation_request_.reset();
    pending_recapture_ = rect;
    KillTimer(window_, recapture_timer);
    // Match the Python client: let the fully transparent result window reach the
    // compositor before capturing the newly resized region.
    SetTimer(window_, recapture_timer, 80, nullptr);
}

void AppHost::recapture_now() {
    if (!pending_recapture_) return;
    const RECT rect = *pending_recapture_;
    pending_recapture_.reset();
    try {
        auto image = std::make_shared<PixelBuffer>(capture_rect(rect));
        last_blocks_.clear();
        start_pipeline(image, rect);
    } catch (const std::exception& error) {
        result_.set_error(L"\u91CD\u65B0\u622A\u53D6\u5931\u8D25\uFF1A" +
                          utf8_to_wide(error.what()));
    }
}

ResultAppearance AppHost::result_appearance() const {
    ResultAppearance value;
    value.font_family = theme::font_family;
    value.minimum_font_pixels = 9;
    return value;
}

void AppHost::open_settings() {
    try {
        settings_.show(window_);
        refresh_tray_tooltip();
    } catch (const std::exception& error) {
        notify(MINITRANS_DISPLAY_NAME, L"无法打开设置：" + utf8_to_wide(error.what()));
    }
}

void AppHost::open_translation_settings() {
    try {
        settings_.show_translation(window_);
    } catch (const std::exception& error) {
        notify(MINITRANS_DISPLAY_NAME, L"无法打开翻译设置：" + utf8_to_wide(error.what()));
    }
}


void AppHost::quit() {
    if (shutting_down_) return;
    shutting_down_ = true;
    PostMessageW(window_, WM_CLOSE, 0, 0);
}

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
            SetTimer(window_, update_timer, 1500, nullptr);
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

int AppHost::run(bool self_test, bool update_self_test,
                 bool text_translation_on_start) {
    self_test_mode_ = self_test || update_self_test;
    update_self_test_mode_ = update_self_test;
    text_translation_on_start_ = text_translation_on_start;
    if (update_self_test) {
        start_silent_update_check();
    }
    if (self_test) {
        PipelineController::self_test();
        if (text_session_) text_session_->self_test();
        tray_menu::layout_self_test();
        HICON brand_icon = tray_menu::load_app_icon(instance_);
        if (!brand_icon) throw AppError("application icon self-test failed");
        DestroyIcon(brand_icon);
        BlockEditor::self_test(instance_, window_);
        settings_.self_test(window_);
        updater_self_test();
        if (quick_window_) quick_window_->self_test(window_);
        if (!PostMessageW(window_, WM_CLOSE, 0, 0)) {
            throw_last_error("queue self-test shutdown");
        }
    }
    MSG message{};
    BOOL result = 0;
    while ((result = GetMessageW(&message, nullptr, 0, 0)) > 0) {
        if (BlockEditor::preprocess_active_message(message) ||
            settings_.preprocess_message(message) ||
            (quick_window_ && quick_window_->preprocess_message(message))) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (result == -1) throw_last_error("message loop failed");
    return static_cast<int>(message.wParam);
}

}  // namespace minitrans
