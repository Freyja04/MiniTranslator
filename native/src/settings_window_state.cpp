#include "settings_window.hpp"
#include "settings_window_internal.hpp"

namespace minitrans {

using namespace settings_internal;

void SettingsWindow::load_values() {
    loading_ = true;
    const auto capture_hotkey = config_.string(L"hotkey", L"Ctrl+Alt+Q");
    SetWindowTextW(capture_hotkey_, capture_hotkey.c_str());
    SetWindowTextW(toggle_hotkey_, config_.string(L"hotkey_toggle", L"Ctrl+Alt+W").c_str());
    SetWindowTextW(text_hotkey_, config_.string(
        L"hotkey_text_translate", L"Ctrl+Alt+Space").c_str());
    const auto configured_provider = config_.string(L"translator.provider", L"microsoft_free");
    const bool large_model = configured_provider == L"ai";
    int provider_position = 0;
    int provider_selection = 0;
    for (const auto& provider : providers) {
        if (provider.kind == ProviderInfo::Kind::ai) continue;
        if (provider.id == configured_provider) provider_selection = provider_position;
        ++provider_position;
    }
    loaded_provider_ = large_model ? std::wstring(L"ai") : configured_provider;
    select_combo(translator_class_, large_model ? 1 : 0);
    select_combo(provider_, large_model ? 0 : provider_selection);
    populate_profile_names();
    select_profile(config_.string(L"translator.ai_selected"));
    refresh_provider_fields(true);
    const auto zh = config_.string(L"lang.zh_target", L"en");
    int zh_index = 0;
    for (std::size_t index = 0; index < zh_targets.size(); ++index) {
        if (zh_targets[index].first == zh) zh_index = static_cast<int>(index);
    }
    select_combo(chinese_target_, zh_index);

    const auto saved_languages = config_.strings(L"ocr.languages");
    int language_index = 0;
    bool language_found = false;
    for (std::size_t index = 0; index < ocr_language_options_.size(); ++index) {
        if (ocr_language_options_[index] == saved_languages) {
            language_index = static_cast<int>(index);
            language_found = true;
            break;
        }
    }
    if (!language_found && !saved_languages.empty()) {
        std::wstring label = L"当前设置（";
        for (const auto& language : saved_languages) {
            if (label.size() > 5) label += L"、";
            label += ocr_language_name(language);
        }
        label += L"）";
        ocr_language_options_.push_back(saved_languages);
        SendMessageW(ocr_languages_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(label.c_str()));
        language_index = static_cast<int>(ocr_language_options_.size() - 1);
    }
    select_combo(ocr_languages_, language_index);
    set_checkbox(ocr_upscale_, config_.boolean(L"ocr.upscale", true));

    const int theme_index = theme::mode == theme::Mode::light ? 1
                          : theme::mode == theme::Mode::dark ? 2 : 0;
    select_combo(theme_mode_, theme_index);
    set_checkbox(auto_copy_, config_.boolean(L"appearance.auto_copy", true));
    set_checkbox(autostart_, autostart_enabled());
    SetWindowTextW(config_path_, config_.path().c_str());
    loading_ = false;
}

std::vector<SettingsWindow::AiProfile> SettingsWindow::read_profiles() const {
    std::vector<AiProfile> list;
    const auto stored = config_.array(L"translator.ai_profiles");
    for (const auto& entry : stored) {
        if (entry.ValueType() != JsonValueType::Object) continue;
        const auto option = entry.GetObject();
        AiProfile profile;
        profile.name = option.GetNamedString(L"name", L"").c_str();
        profile.protocol = option.GetNamedString(L"protocol", L"openai_chat").c_str();
        profile.base_url = option.GetNamedString(L"base_url", L"").c_str();
        profile.key = option.GetNamedString(L"key", L"").c_str();
        profile.model = option.GetNamedString(L"model", L"").c_str();
        if (profile.name.empty()) continue;
        list.push_back(std::move(profile));
    }
    return list;
}

void SettingsWindow::write_profiles(const std::vector<AiProfile>& list) {
    JsonArray array;
    for (const auto& profile : list) {
        JsonObject option;
        option.SetNamedValue(L"name", JsonValue::CreateStringValue(profile.name));
        option.SetNamedValue(L"protocol", JsonValue::CreateStringValue(profile.protocol));
        option.SetNamedValue(L"base_url", JsonValue::CreateStringValue(profile.base_url));
        option.SetNamedValue(L"key", JsonValue::CreateStringValue(profile.key));
        option.SetNamedValue(L"model", JsonValue::CreateStringValue(profile.model));
        array.Append(option);
    }
    config_.set_json(L"translator.ai_profiles", array);
    config_.set_string(L"translator.ai_selected", loaded_profile_);
}

int SettingsWindow::profile_index() const noexcept {
    if (loaded_profile_.empty()) return -1;
    const auto list = read_profiles();
    for (std::size_t index = 0; index < list.size(); ++index) {
        if (list[index].name == loaded_profile_) return static_cast<int>(index);
    }
    return -1;
}

std::wstring SettingsWindow::current_options_json() const {
    if (loaded_provider_ == L"ai") {
        const auto list = read_profiles();
        const int index = profile_index();
        if (index >= 0) {
            const auto& profile = list[static_cast<std::size_t>(index)];
            JsonObject option;
            option.SetNamedValue(L"name", JsonValue::CreateStringValue(profile.name));
            option.SetNamedValue(L"protocol", JsonValue::CreateStringValue(profile.protocol));
            option.SetNamedValue(L"base_url", JsonValue::CreateStringValue(profile.base_url));
            option.SetNamedValue(L"key", JsonValue::CreateStringValue(profile.key));
            option.SetNamedValue(L"model", JsonValue::CreateStringValue(profile.model));
            return option.Stringify().c_str();
        }
        return L"{}";
    }
    return config_.object(L"translator." + loaded_provider_).Stringify().c_str();
}

void SettingsWindow::populate_profile_names() {
    const auto list = read_profiles();
    SendMessageW(profile_select_, CB_RESETCONTENT, 0, 0);
    int selection = -1;
    for (std::size_t index = 0; index < list.size(); ++index) {
        SendMessageW(profile_select_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(list[index].name.c_str()));
        if (list[index].name == loaded_profile_) selection = static_cast<int>(index);
    }
    if (selection >= 0) {
        select_combo(profile_select_, selection);
    } else if (!list.empty()) {
        loaded_profile_ = list.front().name;
        select_combo(profile_select_, 0);
    } else {
        loaded_profile_.clear();
        SetWindowTextW(profile_select_, L"");
    }
    EnableWindow(profile_remove_, !loaded_profile_.empty());
}

void SettingsWindow::select_profile(std::wstring_view name) {
    const auto list = read_profiles();
    bool found = false;
    for (const auto& profile : list) {
        if (profile.name == name) {
            found = true;
            break;
        }
    }
    loaded_profile_ = found ? std::wstring(name)
                            : (list.empty() ? std::wstring() : list.front().name);
    populate_profile_names();
}

void SettingsWindow::add_profile() {
    auto list = read_profiles();
    int suffix = static_cast<int>(list.size()) + 1;
    std::wstring name = L"接口 " + std::to_wstring(suffix);
    const auto taken = [&](std::wstring_view candidate) {
        return std::any_of(list.begin(), list.end(), [&](const AiProfile& profile) {
            return profile.name == candidate;
        });
    };
    while (taken(name)) {
        ++suffix;
        name = L"接口 " + std::to_wstring(suffix);
    }
    AiProfile profile;
    profile.name = name;
    profile.protocol = L"openai_chat";
    list.push_back(profile);
    loaded_profile_ = name;
    write_profiles(list);
    populate_profile_names();
    select_combo(profile_select_, static_cast<int>(list.size() - 1));
    refresh_provider_fields(true);
    save_values(false);
}

void SettingsWindow::remove_profile() {
    auto list = read_profiles();
    if (list.empty()) return;
    const int index = std::clamp(profile_index(), 0, static_cast<int>(list.size()) - 1);
    list.erase(list.begin() + index);
    loaded_profile_ = list.empty() ? std::wstring()
                                   : list[static_cast<std::size_t>(
                                         std::min(index, static_cast<int>(list.size()) - 1))].name;
    write_profiles(list);
    populate_profile_names();
    refresh_provider_fields(false);
    save_values(false);
}

void SettingsWindow::save_profile_fields() {
    if (loaded_provider_ != L"ai") return;
    auto list = read_profiles();
    if (list.empty()) return;
    const int index = profile_index();
    if (index < 0) return;
    auto& profile = list[static_cast<std::size_t>(index)];
    auto renamed = trim(text_of(profile_select_));
    bool rename_happened = false;
    if (!renamed.empty() && renamed != profile.name) {
        // The name is the identity translator.ai_selected points at, so a rename
        // must not collide with another endpoint in the same list.
        const auto taken = [&](std::wstring_view candidate) {
            for (std::size_t other = 0; other < list.size(); ++other) {
                if (other != static_cast<std::size_t>(index) &&
                    list[other].name == candidate) {
                    return true;
                }
            }
            return false;
        };
        if (taken(renamed)) {
            const auto base = renamed;
            int suffix = 2;
            while (taken(renamed)) {
                renamed = base + L" " + std::to_wstring(suffix++);
            }
        }
        profile.name = renamed;
        rename_happened = true;
        SetWindowTextW(profile_select_, profile.name.c_str());
    }
    if (rename_happened) {
        // The combo still holds the old strings, so rebuild it in place and keep
        // the row the user just edited selected.
        SendMessageW(profile_select_, CB_RESETCONTENT, 0, 0);
        for (const auto& entry : list) {
            SendMessageW(profile_select_, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(entry.name.c_str()));
        }
        select_combo(profile_select_, index);
    }
    const int protocol = std::clamp(selected_index(ai_protocol_), 0,
                                    static_cast<int>(ai_protocols.size()) - 1);
    profile.protocol = std::wstring(ai_protocols[static_cast<std::size_t>(protocol)].first);
    profile.base_url = trim(text_of(provider_endpoint_));
    profile.key = trim(text_of(provider_key_));
    profile.model = trim(text_of(provider_model_));
    loaded_profile_ = profile.name;
    write_profiles(list);
}

void SettingsWindow::save_provider_fields() {
    if (loaded_provider_ == L"ai") {
        save_profile_fields();
        return;
    }
    if (loaded_provider_.empty()) return;
    const auto found = std::find_if(providers.begin(), providers.end(), [&](const ProviderInfo& value) {
        return value.id == loaded_provider_;
    });
    if (found == providers.end()) return;
    const auto prefix = L"translator." + loaded_provider_ + L".";
    if (found->kind != ProviderInfo::Kind::free) {
        config_.set_string(prefix + L"key", trim(text_of(provider_key_)));
    }
    if (found->kind == ProviderInfo::Kind::deepl) {
        config_.set_boolean(prefix + L"free_plan", true);
    }
}

void SettingsWindow::refresh_provider_fields(bool load) {
    const bool large_model = selected_index(translator_class_) == 1;
    if (large_model) {
        loaded_provider_ = L"ai";
    } else {
        // Same skip rule as the rest of the page: the combo only lists the
        // traditional entries, so its index counts those rows alone.
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
    }
    const auto traditional = std::find_if(
        providers.begin(), providers.end(),
        [&](const ProviderInfo& value) { return value.id == loaded_provider_; });
    const bool has_key = large_model ||
        (traditional != providers.end() && traditional->kind != ProviderInfo::Kind::free);
    for (const auto [control, visible] : {
             std::pair{provider_label_, !large_model},
             std::pair{provider_, !large_model},
             std::pair{profile_label_, large_model},
             std::pair{profile_select_, large_model},
             std::pair{profile_add_, large_model},
             std::pair{profile_remove_, large_model},
             std::pair{protocol_label_, large_model},
             std::pair{ai_protocol_, large_model},
             std::pair{provider_key_label_, has_key},
             std::pair{provider_key_, has_key},
             std::pair{provider_endpoint_label_, large_model},
             std::pair{provider_endpoint_, large_model},
             std::pair{provider_model_label_, large_model},
             std::pair{provider_model_, large_model},
             std::pair{model_refresh_, large_model},
             std::pair{test_button_, true},
             std::pair{test_status_, true}}) {
        ShowWindow(control, visible ? SW_SHOW : SW_HIDE);
    }

    std::wstring_view key_cue;
    std::wstring_view endpoint_cue;
    std::wstring_view model_cue;
    if (large_model) {
        key_cue = L"sk-...";
        endpoint_cue = L"https://api.deepseek.com/v1";
        model_cue = L"点「获取模型」拉取可用模型";
    } else if (loaded_provider_ == L"deepl") {
        key_cue = L"免费版密钥以 :fx 结尾";
    }
    SendMessageW(provider_key_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(key_cue.data()));
    SendMessageW(provider_endpoint_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(endpoint_cue.data()));
    COMBOBOXINFO model_info{sizeof(model_info)};
    if (GetComboBoxInfo(provider_model_, &model_info) && model_info.hwndItem) {
        SendMessageW(model_info.hwndItem, EM_SETCUEBANNER, TRUE,
                     reinterpret_cast<LPARAM>(model_cue.data()));
    }

    if (load || large_model) {
        if (large_model) {
            const auto list = read_profiles();
            const int index = profile_index();
            const AiProfile empty;
            const auto& profile = index >= 0 ? list[static_cast<std::size_t>(index)] : empty;
            SetWindowTextW(provider_key_, profile.key.c_str());
            SetWindowTextW(provider_endpoint_, profile.base_url.c_str());
            int protocol_index = 0;
            for (std::size_t i = 0; i < ai_protocols.size(); ++i) {
                if (ai_protocols[i].first == profile.protocol) protocol_index = static_cast<int>(i);
            }
            select_combo(ai_protocol_, protocol_index);
            SendMessageW(provider_model_, CB_RESETCONTENT, 0, 0);
            if (!profile.model.empty()) {
                SendMessageW(provider_model_, CB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(profile.model.c_str()));
            }
            SetWindowTextW(provider_model_, profile.model.c_str());
            clear_combo_edit_selection(provider_model_);
        } else {
            const auto prefix = L"translator." + loaded_provider_ + L".";
            SetWindowTextW(provider_key_, config_.string(prefix + L"key").c_str());
            SendMessageW(provider_model_, CB_RESETCONTENT, 0, 0);
            SetWindowTextW(provider_model_, L"");
        }
    }
    SetWindowTextW(provider_hint_,
                   large_model
                       ? L"可以添加多个大模型接口，各自填 Base URL、API Key 和模型；名称用于区分。"
                       : L"这些是免密钥或只需密钥的传统接口，由各服务商的网页/移动端接口提供，可能被限流或随服务改版失效。");
    EnableWindow(profile_remove_, large_model && !loaded_profile_.empty());
    EnableWindow(model_refresh_, large_model && !model_refreshing_);
    layout_controls();
    if (large_model && window_) {
        clear_combo_edit_selection(provider_model_);
        PostMessageW(window_, clear_combo_selection_message,
                     reinterpret_cast<WPARAM>(provider_model_), 0);
    }
}

bool SettingsWindow::save_values(bool) {
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
        const bool large_model = selected_index(translator_class_) == 1;
        if (large_model) {
            config_.set_string(L"translator.provider", L"ai");
        } else {
            // The provider combo lists only the traditional entries, so its
            // selection is a position among the non-AI rows of the catalog.
            const int chosen = selected_index(provider_);
            int seen = 0;
            std::wstring_view selected_provider = providers.front().id;
            for (const auto& provider : providers) {
                if (provider.kind == ProviderInfo::Kind::ai) continue;
                if (seen == chosen) {
                    selected_provider = provider.id;
                    break;
                }
                ++seen;
            }
            config_.set_string(L"translator.provider", selected_provider);
        }
        config_.set_string(L"lang.zh_target",
                           zh_targets[std::clamp(selected_index(chinese_target_), 0,
                                                 static_cast<int>(zh_targets.size() - 1))].first);
        const int language_index = std::clamp(
            selected_index(ocr_languages_), 0,
            std::max(0, static_cast<int>(ocr_language_options_.size()) - 1));
        config_.set_strings(L"ocr.languages",
                            ocr_language_options_[static_cast<std::size_t>(language_index)]);
        config_.set_boolean(L"ocr.upscale", checkbox_checked(ocr_upscale_));
        const int theme_selection = std::clamp(selected_index(theme_mode_), 0, 2);
        const auto theme_mode =
            theme::mode_name(static_cast<theme::Mode>(theme_selection));
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
        const bool config_changed = config_.root().Stringify().c_str() != before;
        if (config_changed) config_.save();
        if (theme_changed) theme::apply_mode(static_cast<theme::Mode>(theme_selection));
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

void SettingsWindow::start_connection_test() {
    if (test_thread_.joinable()) return;
    if (!save_values()) return;
    const auto provider = loaded_provider_;
    const std::wstring options = current_options_json();
    EnableWindow(test_button_, FALSE);
    test_pending_ = true;
    SetWindowTextW(test_status_, L"测试中…");
    const HWND target = window_;
    test_thread_ = std::jthread([target, provider, options](std::stop_token stop) noexcept {
        try {
            auto result = std::make_unique<std::pair<bool, std::wstring>>();
            try {
                WinrtApartment apartment(winrt::apartment_type::multi_threaded);
                TranslatorService service;
                const auto translated = service.translate(
                    provider, winrt::Windows::Data::Json::JsonObject::Parse(options),
                    {L"Hello"}, L"zh-Hans", std::nullopt, stop);
                result->first = translated.size() == 1 && !translated.front().empty();
                result->second = result->first ? L"连接正常" : L"接口没有返回译文";
            } catch (const std::exception& error) {
                result->first = false;
                result->second = redact_sensitive(utf8_to_wide(error.what()));
            } catch (...) {
                result->first = false;
                result->second = L"连接测试遇到未知错误";
            }
            auto* raw = result.release();
            if (!PostMessageW(target, SettingsWindow::test_completed_message, 0,
                              reinterpret_cast<LPARAM>(raw))) {
                delete raw;
            }
        } catch (...) {
            // A background failure must never terminate the tray process.
        }
    });
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
    if (!target) target = window_;
    if (!target) return;
    MSG pending{};
    while (PeekMessageW(&pending, target, test_completed_message,
                        test_completed_message, PM_REMOVE)) {
        delete reinterpret_cast<std::pair<bool, std::wstring>*>(pending.lParam);
    }
}

void SettingsWindow::start_model_refresh() {
    if (model_thread_.joinable()) return;
    if (selected_index(translator_class_) != 1) return;
    if (!save_values()) return;

    const std::wstring provider = loaded_provider_;
    const std::wstring profile = loaded_profile_;
    const std::wstring options = current_options_json();
    const std::uint64_t generation = ++model_generation_;
    model_refreshing_ = true;
    test_pending_ = true;
    test_success_ = false;
    if (GetFocus() == model_refresh_) SetFocus(provider_model_);
    EnableWindow(model_refresh_, FALSE);
    SetWindowTextW(test_status_, L"正在拉取模型列表…");
    InvalidateRect(test_status_, nullptr, TRUE);
    InvalidateRect(model_refresh_, nullptr, FALSE);

    const HWND target = window_;
    model_thread_ = std::jthread(
        [target, generation, provider, profile,
         options](std::stop_token stop) noexcept {
            try {
                auto result = std::make_unique<ModelListResult>();
                result->generation = generation;
                result->provider = provider;
                result->profile = profile;
                try {
                    WinrtApartment apartment(winrt::apartment_type::multi_threaded);
                    TranslatorService service;
                    result->models = service.list_models(
                        winrt::Windows::Data::Json::JsonObject::Parse(options),
                        stop);
                    result->cancelled = stop.stop_requested();
                } catch (const std::exception& error) {
                    result->cancelled = stop.stop_requested();
                    if (!result->cancelled) {
                        result->error = redact_sensitive(utf8_to_wide(error.what()));
                    }
                } catch (...) {
                    result->cancelled = stop.stop_requested();
                    if (!result->cancelled) {
                        result->error = L"拉取模型列表时遇到未知错误";
                    }
                }
                auto* raw = result.release();
                if (!PostMessageW(target, SettingsWindow::models_completed_message,
                                  0, reinterpret_cast<LPARAM>(raw))) {
                    delete raw;
                }
            } catch (...) {
                // Background failures must not escape into the tray process.
            }
        });
}

void SettingsWindow::finish_model_refresh(ModelListResult result) {
    if (model_thread_.joinable()) model_thread_.join();
    if (result.generation != model_generation_ ||
        result.profile != loaded_profile_) {
        return;
    }
    model_refreshing_ = false;
    test_pending_ = false;
    const bool model_provider = selected_index(translator_class_) == 1;
    EnableWindow(model_refresh_, model_provider);
    InvalidateRect(model_refresh_, nullptr, FALSE);
    if (result.cancelled) return;

    if (!result.error.empty() || result.models.empty()) {
        test_success_ = false;
        auto message = result.error.empty()
            ? std::wstring(L"接口没有返回任何模型")
            : std::move(result.error);
        if (message.size() > 160) message.resize(160);
        SetWindowTextW(test_status_, message.c_str());
        InvalidateRect(test_status_, nullptr, TRUE);
        return;
    }

    const auto keep = trim(text_of(provider_model_));
    SendMessageW(provider_model_, CB_RESETCONTENT, 0, 0);
    for (const auto& model : result.models) {
        SendMessageW(provider_model_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(model.c_str()));
    }
    const auto found = std::find(result.models.begin(), result.models.end(), keep);
    const bool kept = found != result.models.end();
    const int selected = kept
        ? static_cast<int>(found - result.models.begin())
        : 0;
    SendMessageW(provider_model_, CB_SETCURSEL, selected, 0);
    clear_combo_edit_selection(provider_model_);
    PostMessageW(window_, clear_combo_selection_message,
                 reinterpret_cast<WPARAM>(provider_model_), 0);

    test_success_ = true;
    std::wstring message = L"拉到 " + std::to_wstring(result.models.size()) +
                           L" 个模型";
    if (!kept) message += L"，已选 " + text_of(provider_model_);
    SetWindowTextW(test_status_, message.c_str());
    InvalidateRect(test_status_, nullptr, TRUE);
    save_values(false);
}

void SettingsWindow::cancel_model_refresh(bool clear_status) noexcept {
    const bool was_refreshing = model_refreshing_;
    ++model_generation_;
    if (model_thread_.joinable()) {
        model_thread_.request_stop();
        model_thread_.join();
    }
    discard_model_refresh_results();
    model_refreshing_ = false;
    if (was_refreshing) test_pending_ = false;
    if (model_refresh_ && IsWindow(model_refresh_)) {
        EnableWindow(model_refresh_, TRUE);
        InvalidateRect(model_refresh_, nullptr, FALSE);
    }
    if (clear_status && test_status_ && IsWindow(test_status_)) {
        test_success_ = false;
        SetWindowTextW(test_status_, L"");
        InvalidateRect(test_status_, nullptr, TRUE);
    }
}

void SettingsWindow::discard_model_refresh_results(HWND target) noexcept {
    if (!target) target = window_;
    if (!target) return;
    MSG pending{};
    while (PeekMessageW(&pending, target, models_completed_message,
                        models_completed_message, PM_REMOVE)) {
        delete reinterpret_cast<ModelListResult*>(pending.lParam);
    }
}

void SettingsWindow::set_status(std::wstring_view message, bool error) {
    status_error_ = error;
    SetWindowTextW(status_, std::wstring(message).c_str());
    ShowWindow(status_, message.empty() ? SW_HIDE : SW_SHOWNA);
    if (!message.empty()) InvalidateRect(status_, nullptr, TRUE);
}


}  // namespace minitrans
