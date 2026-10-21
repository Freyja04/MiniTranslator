#include "settings_window.hpp"
#include "settings_window_internal.hpp"

namespace minitrans {

using namespace settings_internal;

void SettingsWindow::create_controls() {
    create_theme_resources();
    test_success_ = false;
    test_pending_ = false;
    model_refreshing_ = false;
    for (auto& controls : page_controls_) controls.clear();
    navigation_.fill(nullptr);
    for (std::size_t index = 0; index < navigation_.size(); ++index) {
        const int page = navigation_page_order[index];
        navigation_[index] = add_control(
            -1, 0, L"BUTTON", page_names[static_cast<std::size_t>(page)].data(),
            BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP, id_nav_first + static_cast<int>(index));
    }
    status_ = add_control(-1, 0, L"STATIC", L"", SS_LEFT, 0);
    set_font(status_, small_font_);
    ShowWindow(status_, SW_HIDE);
    create_hotkey_page();
    create_translation_page();
    create_ocr_page();
    create_appearance_page();
    create_other_page();
    create_text_translation_page();
    load_values();
    refresh_text_translation();
    // Open on the sidebar's first entry so the highlighted button always
    // matches the page the user sees.
    show_page(navigation_page_order.front());
    layout_controls();
}

void SettingsWindow::create_hotkey_page() {
    add_label(0, L"文本翻译");
    text_hotkey_ = add_control(0, 0, L"EDIT", L"",
                               ES_AUTOHSCROLL | ES_READONLY |
                               WS_BORDER | WS_TABSTOP);
    add_label(0, L"截图翻译");
    capture_hotkey_ = add_control(0, 0, L"EDIT", L"",
                                  ES_AUTOHSCROLL | ES_READONLY |
                                  WS_BORDER | WS_TABSTOP);
    add_label(0, L"收起 / 显示");
    toggle_hotkey_ = add_control(0, 0, L"EDIT", L"",
                                 ES_AUTOHSCROLL | ES_READONLY |
                                 WS_BORDER | WS_TABSTOP);
    SetWindowSubclass(capture_hotkey_, &SettingsWindow::hotkey_proc,
                      hotkey_subclass_id, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(toggle_hotkey_, &SettingsWindow::hotkey_proc,
                      hotkey_subclass_id, reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(text_hotkey_, &SettingsWindow::hotkey_proc,
                      hotkey_subclass_id, reinterpret_cast<DWORD_PTR>(this));
    for (HWND edit : {text_hotkey_, capture_hotkey_, toggle_hotkey_}) {
        SetPropW(edit, L"MiniTranslator.Hotkey", reinterpret_cast<HANDLE>(1));
        SendMessageW(edit, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(px(12, dpi_), px(12, dpi_)));
    }
    add_label(0, L"拖动鼠标    画出要翻译的区域，松手立刻识别并翻译\r\nEsc / 右键    取消这次框选");
}

void SettingsWindow::create_translation_page() {
    add_label(1, L"接口类别");
    translator_class_ = add_control(1, 0, L"COMBOBOX", L"",
                                    CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                                    WS_VSCROLL | WS_TABSTOP,
                                    id_translator_class);
    SendMessageW(translator_class_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"传统翻译 API"));
    SendMessageW(translator_class_, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"大模型翻译 API"));
    provider_label_ = add_label(1, L"接口");
    provider_ = add_control(1, 0, L"COMBOBOX", L"",
                            CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                            WS_VSCROLL | WS_TABSTOP,
                            id_provider);
    for (const auto& provider : providers) {
        if (provider.kind == ProviderInfo::Kind::ai) continue;
        SendMessageW(provider_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(provider.label.data()));
    }
    profile_label_ = add_label(1, L"接口名称");
    profile_select_ = add_control(1, 0, L"COMBOBOX", L"",
                                  CBS_DROPDOWN | CBS_AUTOHSCROLL |
                                      CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                                      WS_VSCROLL | WS_TABSTOP);
    profile_add_ = add_control(1, 0, L"BUTTON", L"新增",
                               BS_OWNERDRAW | WS_TABSTOP, id_profile_add);
    profile_remove_ = add_control(1, 0, L"BUTTON", L"删除",
                                  BS_OWNERDRAW | WS_TABSTOP, id_profile_remove);
    protocol_label_ = add_label(1, L"接口协议");
    ai_protocol_ = add_control(1, 0, L"COMBOBOX", L"",
                               CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                               WS_VSCROLL | WS_TABSTOP,
                               id_ai_protocol);
    for (const auto& [id, label] : ai_protocols) {
        SendMessageW(ai_protocol_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(label.data()));
    }
    provider_key_label_ = add_label(1, L"API Key");
    provider_key_ = add_control(1, 0, L"EDIT", L"",
                                ES_AUTOHSCROLL | ES_PASSWORD | WS_BORDER | WS_TABSTOP);
    provider_endpoint_label_ = add_label(1, L"Base URL");
    provider_endpoint_ = add_control(1, 0, L"EDIT", L"",
                                     ES_AUTOHSCROLL | WS_BORDER | WS_TABSTOP);
    provider_model_label_ = add_label(1, L"模型");
    provider_model_ = add_control(1, 0, L"COMBOBOX", L"",
                                  CBS_DROPDOWN | CBS_AUTOHSCROLL |
                                      CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                                      WS_VSCROLL | WS_TABSTOP);
    model_refresh_ = add_control(1, 0, L"BUTTON", L"获取模型",
                                 BS_OWNERDRAW | WS_TABSTOP, id_refresh_models);
    test_button_ = add_control(1, 0, L"BUTTON", L"测试连接",
                               BS_OWNERDRAW | WS_TABSTOP, id_test);
    test_status_ = add_control(1, 0, L"STATIC", L"",
                               SS_LEFT | SS_CENTERIMAGE, 0);
    set_font(test_status_, small_font_);
    provider_hint_ = add_label(1, L"");
    set_font(provider_hint_, small_font_);
    add_label(1, L"中文译成");
    chinese_target_ = add_control(1, 0, L"COMBOBOX", L"",
                                  CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
                                  WS_VSCROLL | WS_TABSTOP);
    for (const auto& [code, label] : zh_targets) {
        SendMessageW(chinese_target_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.data()));
    }
}

void SettingsWindow::create_ocr_page() {
    add_label(2, L"引擎");
    // Windows OCR is the only engine, so present it as a plain value instead of
    // a one-item drop-down that implies a choice.
    ocr_engine_value_ = add_control(
        2, 0, L"STATIC", L"系统自带 OCR", SS_LEFT | SS_CENTERIMAGE, 0);
    ocr_engine_note_ = add_label(2, L"截图只在本地识别，不会上传到任何服务器。");
    SetWindowLongPtrW(ocr_engine_note_, GWL_STYLE,
                      (GetWindowLongPtrW(ocr_engine_note_, GWL_STYLE) &
                       ~static_cast<LONG_PTR>(SS_TYPEMASK)) |
                          SS_LEFT | SS_CENTERIMAGE);
    set_font(ocr_engine_note_, small_font_);
    add_label(2, L"识别语言");
    ocr_languages_ = add_control(2, 0, L"COMBOBOX", L"",
                                 CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
                                 CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP);
    std::vector<std::wstring> installed;
    try {
        for (const auto& language :
             winrt::Windows::Media::Ocr::OcrEngine::AvailableRecognizerLanguages()) {
            installed.emplace_back(language.LanguageTag().c_str());
        }
    } catch (...) {
        // The rest of the settings window remains usable even if WinRT language discovery fails.
    }
    std::stable_partition(installed.begin(), installed.end(), [](const std::wstring& tag) {
        return tag.starts_with(L"zh") || tag.starts_with(L"ja") || tag.starts_with(L"ko");
    });
    ocr_language_options_.clear();
    if (!installed.empty()) {
        ocr_language_options_.push_back(installed);
        SendMessageW(ocr_languages_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"自动（推荐）"));
        for (const auto& tag : installed) {
            ocr_language_options_.push_back({tag});
            const auto label = L"只用" + ocr_language_name(tag);
            SendMessageW(ocr_languages_, CB_ADDSTRING, 0,
                         reinterpret_cast<LPARAM>(label.c_str()));
        }
    } else {
        ocr_language_options_.push_back({L"zh-Hans-CN", L"en-US"});
        SendMessageW(ocr_languages_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(L"未检测到 Windows OCR 语言包"));
    }
    std::wstring installed_names = L"系统已安装：";
    if (installed.empty()) {
        installed_names += L"无";
    } else {
        for (std::size_t index = 0; index < installed.size(); ++index) {
            if (index) installed_names += L"、";
            installed_names += ocr_language_name(installed[index]);
        }
    }
    const HWND installed_label = add_label(2, installed_names.c_str());
    SetWindowLongPtrW(
        installed_label, GWL_STYLE,
        (GetWindowLongPtrW(installed_label, GWL_STYLE) &
         ~static_cast<LONG_PTR>(SS_TYPEMASK)) |
            SS_LEFT | SS_CENTERIMAGE);
    set_font(installed_label, small_font_);
    // This label is the first row inside the language-install card, so it
    // shares the card's text-color token with the other explanatory copy.
    SetWindowTextW(installed_label, installed_names.c_str());
    ocr_upscale_ = add_control(2, 0, L"BUTTON", L"小图自动放大后识别",
                               BS_OWNERDRAW | WS_TABSTOP);
}

void SettingsWindow::create_appearance_page() {
    add_label(3, L"主题");
    theme_mode_ = add_control(3, 0, L"COMBOBOX", L"",
                              CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
                                  CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
                              id_theme_mode);
    for (const auto mode : theme_modes) {
        SendMessageW(theme_mode_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(mode.data()));
    }
    auto_copy_ = add_control(3, 0, L"BUTTON", L"自动复制到剪贴板",
                             BS_OWNERDRAW | WS_TABSTOP);
    add_label(3, L"复制译文");
}

void SettingsWindow::create_other_page() {
    autostart_ = add_control(4, 0, L"BUTTON", L"开机自动启动",
                             BS_OWNERDRAW | WS_TABSTOP);
    add_label(4, L"配置文件");
    config_path_ = add_control(
        4, 0, L"STATIC", L"",
        SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS);
    open_config_ = add_control(4, 0, L"BUTTON", L"在资源管理器中打开",
                               BS_OWNERDRAW | WS_TABSTOP, id_open_config);
    restart_ = add_control(4, 0, L"BUTTON", L"重启程序",
                           BS_OWNERDRAW | WS_TABSTOP, id_restart);
    check_updates_ = add_control(4, 0, L"BUTTON", L"检查更新",
                                 BS_OWNERDRAW | WS_TABSTOP, id_check_updates);
    add_label(4, L"这里改的每一项都即时写盘。API 密钥用 Windows 的 DPAPI 加过密，只有当前这台电脑上的当前这个账户能解开。");
    diagnostic_command_ = add_control(
        4, 0, L"STATIC", L"MiniTranslator.exe --selftest",
        SS_LEFT | SS_CENTERIMAGE | SS_ENDELLIPSIS);
    copy_diagnostic_ = add_control(4, 0, L"BUTTON", L"复制",
                                   BS_OWNERDRAW | WS_TABSTOP, id_copy_diagnostic);
}

void SettingsWindow::create_text_translation_page() {
    text_target_ = add_control(
        5, 0, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
            WS_VSCROLL | WS_TABSTOP,
        id_text_target);
    populate_text_targets(text_target_);
    SendMessageW(text_target_, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
                 px(32, dpi_));
    SendMessageW(text_target_, CB_SETITEMHEIGHT, 0, px(32, dpi_));
    SendMessageW(text_target_, CB_SETMINVISIBLE, 8, 0);

    text_translate_ = add_control(5, 0, L"BUTTON", L"立即翻译",
                                  BS_OWNERDRAW | WS_TABSTOP,
                                  id_text_translate);
    text_clear_ = add_control(5, 0, L"BUTTON", L"清空",
                              BS_OWNERDRAW | WS_TABSTOP, id_text_clear);
    text_copy_ = add_control(5, 0, L"BUTTON", L"复制译文",
                             BS_OWNERDRAW | WS_TABSTOP, id_text_copy);
    text_input_ = add_control(
        5, 0, L"EDIT", L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL |
            WS_TABSTOP,
        id_text_input);
    text_output_ = add_control(
        5, 0, L"EDIT", L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL |
            WS_TABSTOP,
        id_text_output);
    SendMessageW(text_input_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(px(12, dpi_), px(12, dpi_)));
    SendMessageW(text_output_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(px(12, dpi_), px(12, dpi_)));
    SendMessageW(text_input_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"输入或粘贴需要翻译的文字"));
    SendMessageW(text_output_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"译文会显示在这里"));
}


}  // namespace minitrans
