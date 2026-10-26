#include "settings_window.hpp"
#include "settings_window_internal.hpp"

namespace minitrans {

using namespace settings_internal;

void SettingsWindow::create_controls() {
    create_theme_resources();
    test_success_ = false;
    test_pending_ = false;
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
}

void SettingsWindow::create_translation_page() {
    // 类别 is a two-button switch instead of a drop-down: the two branches
    // are that different, and the list branch hides most of the form anyway.
    add_label(1, L"类别");
    class_traditional_ = add_control(1, 0, L"BUTTON", L"传统翻译",
                                     BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP,
                                     id_class_traditional);
    class_ai_ = add_control(1, 0, L"BUTTON", L"大模型翻译",
                            BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP,
                            id_class_ai);
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
    // The large-model branch is a list of endpoints: the name on the left and
    // 启用 / 编辑 / 删除 on the right, added and edited through a dialog.
    // Rows are action targets, never a selection, so the list box must not grow
    // a selection band that would repaint the row behind the buttons.
    ai_list_caption_ = add_label(1, L"列表");
    ai_list_ = add_control(
        1, 0, L"LISTBOX", L"",
        LBS_NOTIFY | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS |
            LBS_NOINTEGRALHEIGHT | LBS_NOSEL | WS_VSCROLL | WS_TABSTOP, id_ai_list);
    SetWindowSubclass(ai_list_, &SettingsWindow::list_proc, list_subclass_id,
                      reinterpret_cast<DWORD_PTR>(this));
    // This key field belongs to the traditional branch; the large-model key
    // lives in the endpoint dialog with the rest of that endpoint's fields.
    provider_key_label_ = add_label(1, L"API Key");
    provider_key_ = add_control(1, 0, L"EDIT", L"",
                                ES_AUTOHSCROLL | ES_PASSWORD | WS_BORDER | WS_TABSTOP);
    test_button_ = add_control(1, 0, L"BUTTON", L"测试连接",
                               BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP, id_test);
    test_status_ = add_control(1, 0, L"STATIC", L"",
                               SS_LEFT | SS_CENTERIMAGE, 0);
    set_font(test_status_, small_font_);
    ai_add_ = add_control(1, 0, L"BUTTON", L"添加",
                          BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP, id_ai_add);
    provider_hint_ = add_label(1, L"");
    set_font(provider_hint_, small_font_);
}

void SettingsWindow::create_ocr_page() {
    add_label(2, L"引擎");
    // Windows OCR is the only engine, so present it as a plain value instead of
    // a one-item drop-down that implies a choice.
    ocr_engine_value_ = add_control(
        2, 0, L"STATIC", L"系统自带 OCR", SS_LEFT | SS_CENTERIMAGE, 0);
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
    ocr_language_options_ = settings_ocr::build_rows(installed);
    for (const auto& row : ocr_language_options_) {
        SendMessageW(ocr_languages_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(row.label.c_str()));
    }
    // What the recognized text is translated into. Text translation keeps its
    // own choice inside the quick window and text page.
    add_label(2, L"翻译语言");
    translation_target_ = add_control(2, 0, L"COMBOBOX", L"",
                                      CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED |
                                      CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP);
    for (const auto& [code, label] : language_choices) {
        static_cast<void>(code);
        SendMessageW(translation_target_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(label.data()));
    }
    const std::wstring installed_names = settings_ocr::installed_summary(installed);
    const HWND installed_label = add_label(2, installed_names.c_str());
    SetWindowLongPtrW(
        installed_label, GWL_STYLE,
        (GetWindowLongPtrW(installed_label, GWL_STYLE) &
         ~static_cast<LONG_PTR>(SS_TYPEMASK)) |
            SS_LEFT | SS_CENTERIMAGE);
    set_font(installed_label, small_font_);
    ocr_upscale_ = add_control(2, 0, L"BUTTON", L"小图自动放大后识别",
                               BS_OWNERDRAW | WS_TABSTOP);
}

void SettingsWindow::create_other_page() {
    // The theme switch moved here when the 显示 page went away; it opens the
    // page, followed by the startup option and the maintenance buttons. The
    // choice itself is one segmented row of three owner-drawn chips instead of
    // a drop-down, matching the class switch on the translation page.
    add_label(3, L"主题");
    theme_mode_ = add_control(3, 0, L"BUTTON", theme_modes[0].data(),
                              BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP,
                              id_theme_mode);
    for (std::size_t index = 1; index < theme_modes.size(); ++index) {
        theme_segment_[index - 1] = add_control(
            3, 0, L"BUTTON", theme_modes[index].data(),
            BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP,
            index == 1 ? id_theme_light : id_theme_dark);
    }
    autostart_ = add_control(3, 0, L"BUTTON", L"开机自动启动",
                             BS_OWNERDRAW | WS_TABSTOP);
    diagnostics_ = add_control(3, 0, L"BUTTON", L"诊断信息",
                           BS_OWNERDRAW | WS_TABSTOP, id_diagnostics);
    check_updates_ = add_control(3, 0, L"BUTTON", L"检查更新",
                                 BS_OWNERDRAW | WS_TABSTOP, id_check_updates);
    // Inline progress/outcome for the update check, shown right of the button.
    update_status_ = add_label(3, L"");
    SetWindowLongPtrW(update_status_, GWL_STYLE,
                      (GetWindowLongPtrW(update_status_, GWL_STYLE) &
                       ~static_cast<LONG_PTR>(SS_TYPEMASK)) |
                          SS_LEFT | SS_CENTERIMAGE);
    set_font(update_status_, small_font_);
}

void SettingsWindow::create_text_translation_page() {
    // The page header mirrors the quick translation window: capture on the
    // left edge of the card, languages on the right.
    text_capture_ = add_control(4, 0, L"BUTTON", L"",
                                BS_OWNERDRAW | WS_TABSTOP, id_text_capture);
    text_source_ = add_control(
        4, 0, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
            WS_VSCROLL | WS_TABSTOP,
        id_text_source);
    populate_text_sources(text_source_);
    SendMessageW(text_source_, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
                 px(32, dpi_));
    SendMessageW(text_source_, CB_SETITEMHEIGHT, 0, px(32, dpi_));
    SendMessageW(text_source_, CB_SETMINVISIBLE, 8, 0);
    text_swap_ = add_control(4, 0, L"BUTTON", L"",
                             BS_OWNERDRAW | WS_TABSTOP, id_text_swap);
    text_target_ = add_control(
        4, 0, L"COMBOBOX", L"",
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS |
            WS_VSCROLL | WS_TABSTOP,
        id_text_target);
    populate_text_targets(text_target_);
    SendMessageW(text_target_, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1),
                 px(32, dpi_));
    SendMessageW(text_target_, CB_SETITEMHEIGHT, 0, px(32, dpi_));
    SendMessageW(text_target_, CB_SETMINVISIBLE, 8, 0);

    text_clear_ = add_control(4, 0, L"BUTTON", L"",
                              BS_OWNERDRAW | WS_TABSTOP, id_text_clear);
    text_copy_ = add_control(4, 0, L"BUTTON", L"",
                             BS_OWNERDRAW | WS_TABSTOP, id_text_copy);
    text_input_ = add_control(
        4, 0, L"EDIT", L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN | ES_NOHIDESEL |
            WS_TABSTOP,
        id_text_input);
    text_output_ = add_control(
        4, 0, L"EDIT", L"",
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY | ES_NOHIDESEL,
        id_text_output);
    auto_copy_ = add_control(4, 0, L"BUTTON", L"译文自动复制到剪贴板",
                             BS_OWNERDRAW | WS_TABSTOP);
    // No left margin: the text starts at the same x as the 原文/译文 captions.
    SendMessageW(text_input_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(0, px(12, dpi_)));
    SendMessageW(text_output_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(0, px(12, dpi_)));
    SendMessageW(text_input_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"输入或粘贴需要翻译的文字"));
    SendMessageW(text_output_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"译文会显示在这里"));
}


}  // namespace minitrans
