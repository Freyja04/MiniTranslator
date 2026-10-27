#include "settings_window.hpp"
#include "settings_window_internal.hpp"

#include "connection_test.hpp"
#include "diagnostics.hpp"
#include "settings_ai_dialog.hpp"
#include "provider_key_dialog.hpp"
#include "settings_provider.hpp"

#include <algorithm>

namespace minitrans {

using namespace settings_internal;

void SettingsWindow::load_values() {
    loading_ = true;
    const auto capture_hotkey = config_.string(L"hotkey", L"Ctrl+Alt+Q");
    SetWindowTextW(capture_hotkey_, capture_hotkey.c_str());
    SetWindowTextW(toggle_hotkey_, config_.string(L"hotkey_toggle", L"Ctrl+Alt+W").c_str());
    SetWindowTextW(text_hotkey_, config_.string(
        L"hotkey_text_translate", L"Ctrl+Alt+Space").c_str());
    // 「大模型翻译」is a class rather than a row of the provider combo, so the
    // stored provider decides which branch of the page opens.
    const auto configured_provider =
        config_.string(L"translator.provider", settings_provider::default_id());
    loaded_provider_ = configured_provider == L"ai"
        ? std::wstring(L"ai")
        : std::wstring(settings_provider::id_at(
              settings_provider::index_of(configured_provider)));
    const auto traditional = settings_provider::index_of(loaded_provider_);
    select_combo(provider_,
                 traditional == settings_provider::not_listed
                     ? 0 : static_cast<int>(traditional));
    populate_ai_list();
    refresh_provider_fields();
    const auto target = config_.string(L"lang.target", L"zh-Hans");
    int target_index = 0;
    for (std::size_t index = 0; index < language_choices.size(); ++index) {
        if (language_choices[index].first == target) {
            target_index = static_cast<int>(index);
        }
    }
    select_combo(translation_target_, target_index);

    const auto saved_languages = config_.strings(L"ocr.languages");
    const std::size_t saved_row =
        settings_ocr::find_row(ocr_language_options_, saved_languages);
    int language_index = saved_row == settings_ocr::not_found ? 0 : static_cast<int>(saved_row);
    if (saved_row == settings_ocr::not_found && !saved_languages.empty()) {
        // The saved set is no longer offered (a language pack was removed):
        // append it so the page still shows what will be used.
        ocr_language_options_.push_back(settings_ocr::saved_row(saved_languages));
        SendMessageW(ocr_languages_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(ocr_language_options_.back().label.c_str()));
        language_index = static_cast<int>(ocr_language_options_.size() - 1);
    }
    select_combo(ocr_languages_, language_index);
    set_checkbox(ocr_upscale_, config_.boolean(L"ocr.upscale", true));

    const int theme_index = theme::mode == theme::Mode::light ? 1
                          : theme::mode == theme::Mode::dark ? 2 : 0;
    set_theme_selection(theme_index);
    set_checkbox(auto_copy_, config_.boolean(L"appearance.auto_copy", true));
    set_checkbox(autostart_, autostart_enabled());
    loading_ = false;
}

std::vector<SettingsWindow::AiProfile> SettingsWindow::read_profiles() const {
    // Normalising on the way in means a hand-edited file can never show two
    // enabled endpoints, which is what the pipeline rule already assumes.
    auto list = settings_profiles::parse_profiles(
        config_.array(L"translator.ai_profiles"));
    settings_profiles::normalize_enabled(list);
    return list;
}

void SettingsWindow::write_profiles(const std::vector<AiProfile>& list) {
    config_.set_json(L"translator.ai_profiles",
                     settings_profiles::serialize_profiles(list));
    // save_values() compares the root against its own snapshot, which a list
    // write already changed, so the change is remembered here instead.
    profile_write_dirty_ = true;
}

std::wstring SettingsWindow::current_options_json() const {
    if (large_model()) {
        // Only the endpoint the user enabled can serve a request; an empty
        // object lets the pipeline fall back to the key-free providers.
        for (const auto& profile : read_profiles()) {
            if (profile.enabled && settings_profiles::is_configured(profile)) {
                return settings_profiles::to_options(profile).Stringify().c_str();
            }
        }
        return L"{}";
    }
    return config_.object(L"translator." + loaded_provider_).Stringify().c_str();
}

void SettingsWindow::populate_ai_list() {
    ai_rows_ = settings_ai_list::make_rows(read_profiles());
    if (!ai_list_ || !IsWindow(ai_list_)) return;
    // Rows are drawn with a fixed height so painting and hit testing share one
    // geometry, and the item height is the band the layout reserves per row.
    const int row_height = px(settings_ai_list::logical_row_height(), dpi_);
    // LB_SETITEMHEIGHT repaints the whole list, so it is only sent when the
    // measured row really changed.
    if (row_height != ai_row_height_) {
        SendMessageW(ai_list_, LB_SETITEMHEIGHT, 0,
                     static_cast<LPARAM>(row_height));
        ai_row_height_ = row_height;
    }
    // 启用 / 停用 keeps the order and every name, so the strings can stay where
    // they are: resetting the content would repaint the list from scratch and
    // the row under the pointer would visibly flicker on each click.
    const int stored = static_cast<int>(SendMessageW(ai_list_, LB_GETCOUNT, 0, 0));
    bool unchanged = stored == static_cast<int>(ai_rows_.size());
    for (std::size_t index = 0; unchanged && index < ai_rows_.size(); ++index) {
        const std::wstring_view name = ai_rows_[index].name;
        // The list can hold a longer caption than the one being compared (a
        // hand-edited config file), so the buffer is sized from the stored text.
        const LRESULT length = SendMessageW(ai_list_, LB_GETTEXTLEN,
                                            static_cast<WPARAM>(index), 0);
        if (length != static_cast<LRESULT>(name.size())) {
            unchanged = false;
            break;
        }
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        unchanged = SendMessageW(ai_list_, LB_GETTEXT, static_cast<WPARAM>(index),
                                 reinterpret_cast<LPARAM>(text.data())) != LB_ERR &&
                    std::wstring_view(text.data()) == name;
    }
    if (unchanged) {
        // The rows are where they were and the pointer has not moved, so the
        // highlight is kept: clearing it here would drop and re-add a hover
        // repaint around the click that is already redrawing the row.
        // Only rows whose state the list is not showing yet are invalidated; a
        // toggle changes one row's dot and caption, and invalidating the whole
        // list made every other row re-run its owner-draw too.
        for (std::size_t index = 0; index < ai_rows_.size(); ++index) {
            const auto& shown = index < ai_rows_shown_.size()
                ? ai_rows_shown_[index] : settings_ai_list::Row{};
            if (shown.enabled != ai_rows_[index].enabled ||
                shown.configured != ai_rows_[index].configured) {
                settings_ai_list::invalidate_row(ai_list_,
                                                 static_cast<int>(index));
            }
        }
        ai_rows_shown_ = ai_rows_;
        return;
    }
    // Resetting the content drops the scroll position, so the first visible row
    // is restored once the strings are back.
    const int top_index = static_cast<int>(SendMessageW(ai_list_, LB_GETTOPINDEX, 0, 0));
    SendMessageW(ai_list_, LB_RESETCONTENT, 0, 0);
    for (const auto& row : ai_rows_) {
        SendMessageW(ai_list_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(row.name.c_str()));
    }
    if (top_index > 0 && !ai_rows_.empty()) {
        const int restore = std::min(top_index,
                                     static_cast<int>(ai_rows_.size()) - 1);
        SendMessageW(ai_list_, LB_SETTOPINDEX, static_cast<WPARAM>(restore), 0);
    }
    ai_list_hover_ = -1;
    ai_hover_button_ = settings_ai_list::Button::none;
    ai_rows_shown_ = ai_rows_;
}

const std::vector<settings_ai_list::Row>& SettingsWindow::shown_rows()
    const noexcept {
    return large_model() ? ai_rows_ : traditional_rows_;
}

void SettingsWindow::populate_traditional_list() {
    traditional_rows_.clear();
    for (const auto& provider : providers) {
        if (provider.kind == ProviderInfo::Kind::ai) continue;
        const bool enabled = loaded_provider_ == provider.id;
        settings_ai_list::Row row;
        row.name = std::wstring(provider.label);
        row.enabled = enabled;
        row.configured = provider.kind != ProviderInfo::Kind::deepl ||
                         !config_.string(L"translator.deepl.key").empty();
        row.actions = provider.kind == ProviderInfo::Kind::deepl
            ? settings_ai_list::Row::Actions::toggle_edit
            : settings_ai_list::Row::Actions::toggle;
        traditional_rows_.push_back(std::move(row));
    }
    if (!ai_list_ || !IsWindow(ai_list_)) return;
    const int row_height = px(settings_ai_list::logical_row_height(), dpi_);
    if (row_height != ai_row_height_) {
        SendMessageW(ai_list_, LB_SETITEMHEIGHT, 0,
                     static_cast<LPARAM>(row_height));
        ai_row_height_ = row_height;
    }
    const int top_index =
        static_cast<int>(SendMessageW(ai_list_, LB_GETTOPINDEX, 0, 0));
    SendMessageW(ai_list_, LB_RESETCONTENT, 0, 0);
    for (const auto& row : traditional_rows_) {
        SendMessageW(ai_list_, LB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(row.name.c_str()));
    }
    if (top_index > 0 && !traditional_rows_.empty()) {
        SendMessageW(ai_list_, LB_SETTOPINDEX,
                     static_cast<WPARAM>(std::min(
                         top_index,
                         static_cast<int>(traditional_rows_.size()) - 1)),
                     0);
    }
    ai_list_hover_ = -1;
    ai_hover_button_ = settings_ai_list::Button::none;
}

void SettingsWindow::select_traditional(std::size_t index) {
    if (index >= traditional_rows_.size()) return;
    const auto& provider = providers[index];
    if (provider.kind == ProviderInfo::Kind::deepl) {
        // DeepL edits its key through the small one-field dialog; enabling it
        // without a key is what the edit is for.
        ProviderKeyDialog::Result result = ProviderKeyDialog::edit(
            instance_, window_, provider.label,
            config_.string(L"translator.deepl.key"));
        if (!result.saved) return;
        config_.set_string(L"translator.deepl.key", result.key);
        loaded_provider_ = std::wstring(provider.id);
        config_.set_string(L"translator.provider", loaded_provider_);
        finish_ai_list_change();
        return;
    }
    loaded_provider_ = std::wstring(provider.id);
    config_.set_string(L"translator.provider", loaded_provider_);
    finish_ai_list_change();
}

void SettingsWindow::schedule_profiles_save() {
    // The in-memory config is already written by write_profiles(), so the
    // pipeline sees the new endpoint at once. The disk write itself moves
    // through the shared config_save_timer when save_values() next runs; this
    // call only fans the settings-changed notification out so dependent views
    // refresh at once.
    if (settings_changed_callback_) settings_changed_callback_();
}

void SettingsWindow::flush_profiles_save() {
    // Legacy entry point kept for the close/restart paths: persist now.
    flush_config_save();
}

void SettingsWindow::select_ai_profile(std::size_t index) {
    auto list = read_profiles();
    if (index >= list.size()) return;
    if (list[index].enabled) {
        // 停用 turns large-model translation off instead of silently promoting
        // another endpoint; the pipeline falls back to the key-free providers.
        list[index].enabled = false;
    } else if (settings_profiles::is_configured(list[index])) {
        settings_profiles::select_enabled(list, index);
    } else {
        set_status(L"这个接口还缺少 Base URL 或模型，先编辑它", true);
        return;
    }
    settings_profiles::normalize_enabled(list);
    write_profiles(list);
    finish_ai_list_change();
}

void SettingsWindow::edit_ai_profile(std::optional<std::size_t> index) {
    auto list = read_profiles();
    AiProfile draft;
    std::wstring original_name;
    if (index && *index < list.size()) {
        draft = list[*index];
        original_name = draft.name;
    } else {
        original_name = settings_profiles::next_default_name(list);
        draft.name = original_name;
        draft.protocol = L"openai_chat";
    }
    const auto result = SettingsAiDialog::edit(instance_, window_, draft,
                                               original_name,
                                               draft.name);
    if (!result.saved) return;
    AiProfile profile = result.profile;
    if (const auto existing = settings_profiles::find_index(list, original_name)) {
        const std::size_t at = *existing;
        if (profile.name != original_name) {
            // The name identifies the row, so a rename must not collide with
            // another endpoint in the same list.
            profile.name = settings_profiles::unique_rename(list, at, profile.name);
        }
        // The dialog edits the endpoint, not the switch, so the enable flag
        // stays where the list left it.
        profile.enabled = list[at].enabled;
        list[at] = std::move(profile);
    } else {
        if (settings_profiles::find_index(list, profile.name)) {
            profile.name = settings_profiles::unique_rename(
                list, list.size(), profile.name);
        }
        // A brand new endpoint takes the switch on when nothing else is
        // enabled, so adding one is enough to start translating.
        const bool any_enabled = std::any_of(
            list.begin(), list.end(),
            [](const AiProfile& value) { return value.enabled; });
        profile.enabled = !any_enabled && settings_profiles::is_configured(profile);
        list.push_back(std::move(profile));
    }
    settings_profiles::normalize_enabled(list);
    write_profiles(list);
    finish_ai_list_change();
}

void SettingsWindow::remove_ai_profile(std::size_t index) {
    auto list = read_profiles();
    if (index >= list.size()) return;
    list.erase(list.begin() + static_cast<std::ptrdiff_t>(index));
    settings_profiles::normalize_enabled(list);
    write_profiles(list);
    reset_delete_confirmation();
    finish_ai_list_change();
}

void SettingsWindow::finish_ai_list_change() {
    // Re-lays the page out and repaints it: the list band and the card bottom
    // both follow the row count, and MoveWindow(TRUE) never repaints the
    // parent-drawn card, so without this the new row hangs outside a stale
    // card until something else invalidates the window.
    populate_ai_list();
    schedule_profiles_save();
    refresh_provider_fields();
    if (window_ && IsWindow(window_)) {
        RedrawWindow(window_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
    }
}

void SettingsWindow::reset_delete_confirmation() noexcept {
    if (window_) KillTimer(window_, delete_confirm_timer);
    delete_deadline_ = 0;
    if (pending_delete_.empty()) return;
    const std::size_t index = static_cast<std::size_t>(
        std::find_if(ai_rows_.begin(), ai_rows_.end(),
                     [this](const settings_ai_list::Row& row) {
                         return row.name == pending_delete_;
                     }) - ai_rows_.begin());
    pending_delete_.clear();
    // Only the row that showed 「确认?」 needs its caption back; wiping the
    // whole list here added a second repaint around every other click.
    if (ai_list_ && IsWindow(ai_list_) && index < ai_rows_.size()) {
        settings_ai_list::invalidate_row(ai_list_, static_cast<int>(index));
    }
}

void SettingsWindow::save_provider_fields() {
    // The large-model branch keeps its fields inside the endpoint rows, which
    // the edit dialog writes directly; nothing here has to be collected.
    if (large_model() || loaded_provider_.empty()) return;
    const auto prefix = L"translator." + loaded_provider_ + L".";
    if (settings_provider::needs_key(loaded_provider_)) {
        config_.set_string(prefix + L"key", trim(text_of(provider_key_)));
    }
    if (loaded_provider_ == L"deepl") {
        config_.set_boolean(prefix + L"free_plan", true);
    }
}

void SettingsWindow::refresh_provider_fields() {
    const auto controls = page_controls_.size() > 1 ? page_controls_[1]
                                                    : std::vector<HWND>{};
    const HWND class_label =
        controls.size() > translation_index::class_label ? controls[translation_index::class_label]
                                                         : nullptr;
    if (large_model()) {
        reset_delete_confirmation();
    }
    // The traditional provider choice lives in loaded_provider_ and is set by
    // select_traditional(); the retired combo no longer overrides it here.
    const bool ai = large_model();
    if (!ai) {
        // The traditional branch is the same list control, showing the
        // key-free providers and DeepL in place of the endpoint rows.
        populate_traditional_list();
        SetWindowTextW(ai_list_caption_, L"接口");
    } else {
        SetWindowTextW(ai_list_caption_, L"列表");
    }
    for (const auto [control, visible] : {
             std::pair{class_label, true},
             std::pair{class_traditional_, true},
             std::pair{class_ai_, true},
             std::pair{provider_label_, false},
             std::pair{provider_, false},
             std::pair{provider_key_label_, false},
             std::pair{provider_key_, false},
             std::pair{test_button_, false},
             std::pair{test_status_, false},
             std::pair{ai_list_caption_, true},
             // 添加 stays visible on both branches and is disabled while the
             // traditional providers are showing (layout_controls flips it).
             std::pair{ai_add_, true},
             std::pair{ai_list_, !shown_rows().empty()},
             std::pair{provider_hint_, false}}) {
        ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
    }
    layout_controls();
}

bool SettingsWindow::save_values() {
    if (loading_) return true;
    const std::wstring before = config_.root().Stringify().c_str();
    const auto old_capture = config_.string(L"hotkey", L"Ctrl+Alt+Q");
    const auto old_toggle = config_.string(L"hotkey_toggle", L"Ctrl+Alt+W");
    const auto old_text = config_.string(
        L"hotkey_text_translate", L"Ctrl+Alt+Space");
    const bool old_autostart = config_.boolean(L"autostart", false);
    bool applied_hotkeys = false;
    try {
        const auto capture = trim(text_of(capture_hotkey_));
        const auto toggle = trim(text_of(toggle_hotkey_));
        const auto text = trim(text_of(text_hotkey_));
        std::wstring error;
        const auto capture_parsed = parse_hotkey(capture, &error);
        if (!capture_parsed) throw AppError(wide_to_utf8(error));
        const auto toggle_parsed = parse_hotkey(toggle, &error);
        if (!toggle_parsed) throw AppError(wide_to_utf8(error));
        const auto text_parsed = parse_hotkey(text, &error);
        if (!text_parsed) throw AppError(wide_to_utf8(error));
        if (same_hotkey(*capture_parsed, *toggle_parsed) ||
            same_hotkey(*capture_parsed, *text_parsed) ||
            same_hotkey(*toggle_parsed, *text_parsed)) {
            throw AppError("三个快捷键不能重复");
        }
        config_.set_string(L"hotkey", capture);
        config_.set_string(L"hotkey_toggle", toggle);
        config_.set_string(L"hotkey_text_translate", text);
        const bool hotkeys_changed = capture != old_capture || toggle != old_toggle ||
                                     text != old_text;
        save_provider_fields();
        config_.set_string(L"translator.provider", loaded_provider_);
        config_.set_string(L"lang.target",
                           language_choices[std::clamp(
                               selected_index(translation_target_), 0,
                               static_cast<int>(language_choices.size() - 1))].first);
        if (!ocr_language_options_.empty()) {
            const std::size_t language_index = settings_ocr::clamp_selection(
                selected_index(ocr_languages_), ocr_language_options_.size());
            config_.set_strings(L"ocr.languages",
                                ocr_language_options_[language_index].languages);
        }
        config_.set_boolean(L"ocr.upscale", checkbox_checked(ocr_upscale_));
        const int theme_choice = std::clamp(theme_selection(), 0, 2);
        const auto theme_mode =
            theme::mode_name(static_cast<theme::Mode>(theme_choice));
        const bool theme_changed = config_.string(L"appearance.theme", L"system") !=
                                   theme_mode;
        config_.set_string(L"appearance.theme", theme_mode);
        config_.set_boolean(L"appearance.auto_copy", checkbox_checked(auto_copy_));
        const bool autostart = checkbox_checked(autostart_);
        if (autostart != old_autostart) set_autostart(autostart);
        config_.set_boolean(L"autostart", autostart);
        if (hotkeys_changed && hotkeys_changed_callback_) {
            if (!hotkeys_changed_callback_()) {
                config_.set_string(L"hotkey", old_capture);
                config_.set_string(L"hotkey_toggle", old_toggle);
                config_.set_string(L"hotkey_text_translate", old_text);
                SetWindowTextW(capture_hotkey_, old_capture.c_str());
                SetWindowTextW(toggle_hotkey_, old_toggle.c_str());
                SetWindowTextW(text_hotkey_, old_text.c_str());
                hotkeys_changed_callback_();
                throw AppError("快捷键未生效，可能已被其他程序占用；已恢复原设置");
            }
            applied_hotkeys = true;
        }
        const bool config_changed = profile_write_dirty_ ||
                                    config_.root().Stringify().c_str() != before;
        if (config_changed) {
            profile_write_dirty_ = false;
            // The DPAPI round trip and disk write move to a short debounce:
            // the in-memory config already serves the pipeline and callbacks,
            // so a burst of interactions costs one write instead of one per
            // control. flush_config_save() covers the paths that must not
            // wait (close, restart, update).
            config_save_pending_ = true;
            if (window_ && IsWindow(window_)) {
                SetTimer(window_, config_save_timer, config_save_debounce_ms, nullptr);
            }
        }
        if (theme_changed) theme::apply_mode(static_cast<theme::Mode>(theme_choice));
        if (config_changed && settings_changed_callback_) settings_changed_callback_();
        set_status(L"");
        return true;
    } catch (const std::exception& error) {
        if (applied_hotkeys && hotkeys_changed_callback_) {
            config_.set_string(L"hotkey", old_capture);
            config_.set_string(L"hotkey_toggle", old_toggle);
            config_.set_string(L"hotkey_text_translate", old_text);
            SetWindowTextW(capture_hotkey_, old_capture.c_str());
            SetWindowTextW(toggle_hotkey_, old_toggle.c_str());
            SetWindowTextW(text_hotkey_, old_text.c_str());
            hotkeys_changed_callback_();
        }
        const auto message = utf8_to_wide(error.what());
        set_status(message, true);
        return false;
    }
}

void SettingsWindow::flush_config_save() {
    if (!config_save_pending_) return;
    config_save_pending_ = false;
    if (window_ && IsWindow(window_)) KillTimer(window_, config_save_timer);
    config_.save();
}

void SettingsWindow::open_diagnostics_folder() {
    // The report doubles as the proof a failure happened: exporting and then
    // revealing the file keeps the user one click away from sending it over.
    const auto path = diagnostics::export_report();
    if (path.empty()) {
        set_status(L"诊断信息导出失败", true);
        return;
    }
    set_status(L"");
    const std::wstring arguments = L"/select,\"" + path + L"\"";
    const INT_PTR result = reinterpret_cast<INT_PTR>(
        ShellExecuteW(window_, L"open", L"explorer.exe", arguments.c_str(),
                      nullptr, SW_SHOWNORMAL));
    if (result <= 32) {
        set_status(L"无法打开诊断信息所在目录", true);
    }
}

void SettingsWindow::start_connection_test() {
    if (test_thread_.joinable()) return;
    if (large_model()) {
        // This branch has no 「测试连接」 button: the endpoint dialog owns the
        // test, so nothing here may quietly fall back to a free provider.
        return;
    }
    if (!save_values()) return;
    const auto provider = loaded_provider_;
    const std::wstring options = current_options_json();
    EnableWindow(test_button_, FALSE);
    test_pending_ = true;
    SetWindowTextW(test_status_, L"测试中…");
    test_thread_ = connection_test::run(window_, test_completed_message, provider,
                                        options);
}

void SettingsWindow::finish_connection_test(bool success, std::wstring message) {
    if (test_thread_.joinable()) test_thread_.join();
    EnableWindow(test_button_, TRUE);
    test_pending_ = false;
    test_success_ = success;
    SetWindowTextW(test_status_, message.c_str());
    InvalidateRect(test_status_, nullptr, TRUE);
    InvalidateRect(test_button_, nullptr, FALSE);
}

void SettingsWindow::discard_connection_test_results(HWND target) noexcept {
    connection_test::discard(target ? target : window_, test_completed_message);
}

void SettingsWindow::set_status(std::wstring_view message, bool error) {
    status_error_ = error;
    SetWindowTextW(status_, std::wstring(message).c_str());
    ShowWindow(status_, message.empty() ? SW_HIDE : SW_SHOWNA);
    if (!message.empty()) InvalidateRect(status_, nullptr, TRUE);
}

void SettingsWindow::set_update_status(std::wstring_view text, bool error) {
    update_status_error_ = error;
    if (!update_status_ || !IsWindow(update_status_)) return;
    SetWindowTextW(update_status_, std::wstring(text).c_str());
    InvalidateRect(update_status_, nullptr, TRUE);
}

void SettingsWindow::set_update_check_enabled(bool enabled) {
    if (!check_updates_ || !IsWindow(check_updates_)) return;
    EnableWindow(check_updates_, enabled ? TRUE : FALSE);
}


}  // namespace minitrans
