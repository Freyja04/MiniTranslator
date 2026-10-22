#include "settings_window.hpp"
#include "settings_window_internal.hpp"

namespace minitrans {

using namespace settings_internal;

void SettingsWindow::self_test(HWND owner) {
    if (window_) throw AppError("settings self-test window already exists");
    create_window(owner);
    if (!window_ || !capture_hotkey_ || !toggle_hotkey_ || !text_hotkey_ ||
        !provider_ || !ocr_engine_value_ || !translation_target_ ||
        !navigation_[0] || !navigation_[1] ||
        !text_session_ || !text_capture_ || !text_source_ || !text_swap_ ||
        !text_target_ || !text_input_ || !text_output_ || !text_clear_ ||
        !text_copy_ || !update_status_) {
        throw AppError("settings self-test did not create required controls");
    }
    constexpr std::array<std::wstring_view, 5> expected_navigation{
        L"翻译", L"截图识别", L"翻译接口",
        L"快捷键", L"设置",
    };
    for (std::size_t index = 0; index < expected_navigation.size(); ++index) {
        if (!navigation_[index] ||
            text_of(navigation_[index]) != expected_navigation[index]) {
            throw AppError("settings self-test navigation order mismatch");
        }
    }
    constexpr LONG_PTR required_extended_style = WS_EX_APPWINDOW | WS_EX_COMPOSITED;
    if ((GetWindowLongPtrW(window_, GWL_EXSTYLE) & required_extended_style) !=
            required_extended_style ||
        GetWindow(window_, GW_OWNER) != nullptr || !icon_font_) {
        throw AppError("settings self-test did not create an independent composited window");
    }
    RECT client{};
    GetClientRect(window_, &client);
    if (client.right < px(760, dpi_) ||
        client.bottom < px(logical_window_height, dpi_) - px(20, dpi_)) {
        throw AppError("settings self-test client size differs from Python reference");
    }
    if ((GetWindowLongPtrW(status_, GWL_STYLE) & WS_VISIBLE) != 0) {
        throw AppError("settings self-test empty footer status remained visible");
    }
    set_status(L"status visibility test", true);
    if ((GetWindowLongPtrW(status_, GWL_STYLE) & WS_VISIBLE) == 0) {
        throw AppError("settings self-test non-empty footer status remained hidden");
    }
    set_status(L"");
    if ((GetWindowLongPtrW(status_, GWL_STYLE) & WS_VISIBLE) != 0) {
        throw AppError("settings self-test cleared footer status remained visible");
    }
    if (SendMessageW(provider_, CB_GETCOUNT, 0, 0) !=
        static_cast<LRESULT>(providers.size() - 1)) {
        throw AppError("settings self-test provider count mismatch");
    }
    constexpr std::array<std::wstring_view, 5> expected_providers{
        L"腾讯交互翻译（免密钥）", L"微软翻译（免密钥）", L"词霸翻译（免密钥）",
        L"Yandex 翻译（免密钥）", L"DeepL",
    };
    for (std::size_t index = 0; index < expected_providers.size(); ++index) {
        wchar_t label[128]{};
        SendMessageW(provider_, CB_GETLBTEXT, static_cast<WPARAM>(index),
                     reinterpret_cast<LPARAM>(label));
        if (std::wstring_view(label) != expected_providers[index]) {
            throw AppError("settings self-test provider ordering mismatch");
        }
    }
    if (SendMessageW(translator_class_, CB_GETCOUNT, 0, 0) != 2) {
        throw AppError("settings self-test translator class count mismatch");
    }
    if (SendMessageW(ai_protocol_, CB_GETCOUNT, 0, 0) !=
        static_cast<LRESULT>(ai_protocols.size())) {
        throw AppError("settings self-test large-model protocol count mismatch");
    }
    wchar_t ocr_engine_class[16]{};
    if (!GetClassNameW(ocr_engine_value_, ocr_engine_class,
                       static_cast<int>(std::size(ocr_engine_class))) ||
        std::wstring_view(ocr_engine_class) != L"Static" ||
        text_of(ocr_engine_value_) != L"系统自带 OCR") {
        throw AppError("settings self-test OCR engine display mismatch");
    }
    show_page(0);
    const auto& hotkey_controls = page_controls_[0];
    if (hotkey_controls.size() <= hotkey_index::toggle_edit ||
        text_of(hotkey_controls[hotkey_index::text_label]) != L"文本翻译" ||
        hotkey_controls[hotkey_index::text_edit] != text_hotkey_ ||
        text_of(hotkey_controls[hotkey_index::capture_label]) != L"截图翻译" ||
        hotkey_controls[hotkey_index::capture_edit] != capture_hotkey_ ||
        text_of(hotkey_controls[hotkey_index::toggle_label]) != L"收起 / 显示" ||
        hotkey_controls[hotkey_index::toggle_edit] != toggle_hotkey_ ||
        text_of(text_hotkey_) != L"Ctrl+Alt+Space") {
        throw AppError("settings self-test hotkey order mismatch");
    }
    // Hotkey parsing and duplicate detection are pure logic and are covered by
    // core_self_test(); only the control wiring above stays window-bound.
    RECT hotkey_rect{};
    if (!GetWindowRect(capture_hotkey_, &hotkey_rect)) {
        throw AppError("settings self-test could not measure hotkey fields");
    }
    const int hotkey_width = hotkey_rect.right - hotkey_rect.left;
    const int hotkey_height = hotkey_rect.bottom - hotkey_rect.top;
    const int expected_hotkey_height = px(34, dpi_) -
        std::max(1, px(logical_edit_inset_top, dpi_)) -
        std::max(1, px(logical_edit_inset_bottom, dpi_));
    RECT toggle_rect{};
    RECT text_rect{};
    const int minimum_stride = px(logical_control_height + logical_row_gap,
                                  dpi_);
    const int width_tolerance = std::max(1, px(2, dpi_));
    if (!GetWindowRect(toggle_hotkey_, &toggle_rect) ||
        !GetWindowRect(text_hotkey_, &text_rect) ||
        std::abs(hotkey_width - (toggle_rect.right - toggle_rect.left)) >
            width_tolerance ||
        std::abs(hotkey_width - (text_rect.right - text_rect.left)) >
            width_tolerance ||
        std::abs(hotkey_rect.right - toggle_rect.right) > width_tolerance ||
        std::abs(hotkey_rect.right - text_rect.right) > width_tolerance ||
        hotkey_rect.top - text_rect.top < minimum_stride ||
        toggle_rect.top - hotkey_rect.top < minimum_stride ||
        std::abs(hotkey_height - expected_hotkey_height) > 2) {
        throw AppError("settings self-test hotkey text inset mismatch");
    }
    if ((GetWindowLongPtrW(capture_hotkey_, GWL_STYLE) & WS_BORDER) != 0 ||
        (GetWindowLongPtrW(capture_hotkey_, GWL_EXSTYLE) &
             (WS_EX_CLIENTEDGE | WS_EX_STATICEDGE)) != 0) {
        throw AppError("settings self-test edit field retained a square system frame");
    }
    HRGN hotkey_region = CreateRectRgn(0, 0, 1, 1);
    const int region_type = hotkey_region
        ? GetWindowRgn(capture_hotkey_, hotkey_region) : ERROR;
    RECT hotkey_client{};
    GetClientRect(capture_hotkey_, &hotkey_client);
    const bool rounded_region = region_type == COMPLEXREGION &&
        !PtInRegion(hotkey_region, 0, 0) &&
        PtInRegion(hotkey_region, hotkey_client.right / 2,
                   hotkey_client.bottom / 2);
    if (hotkey_region) DeleteObject(hotkey_region);
    if (!rounded_region) {
        throw AppError("settings self-test edit field did not receive rounded corners");
    }
    for (const HWND edit : {text_hotkey_, capture_hotkey_, toggle_hotkey_}) {
        const LRESULT margins = SendMessageW(edit, EM_GETMARGINS, 0, 0);
        if ((GetWindowLongPtrW(edit, GWL_STYLE) &
             static_cast<LONG_PTR>(ES_CENTER)) != 0 ||
            LOWORD(margins) < px(10, dpi_) ||
            HIWORD(margins) < px(10, dpi_)) {
            throw AppError("settings self-test hotkey text is not left aligned");
        }
    }
    show_page(4);
    if (SendMessageW(text_source_, CB_GETCOUNT, 0, 0) != 9) {
        throw AppError("settings self-test text source list changed size");
    }
    if (SendMessageW(text_target_, CB_GETCOUNT, 0, 0) != 10) {
        throw AppError("settings self-test text translation page is incomplete");
    }
    COMBOBOXINFO text_target_info{sizeof(text_target_info)};
    RECT text_target_list{};
    if (!GetComboBoxInfo(text_target_, &text_target_info) ||
        !text_target_info.hwndList ||
        !GetWindowRect(text_target_info.hwndList, &text_target_list) ||
        text_target_list.bottom - text_target_list.top < px(200, dpi_)) {
        throw AppError("settings self-test text target menu is clipped");
    }
    RECT text_input_rect{};
    RECT text_output_rect{};
    if (!GetWindowRect(text_input_, &text_input_rect) ||
        !GetWindowRect(text_output_, &text_output_rect) ||
        text_input_rect.right - text_input_rect.left < px(480, dpi_) ||
        text_output_rect.bottom - text_output_rect.top < px(120, dpi_)) {
        throw AppError("settings self-test text translation layout is invalid");
    }
    RECT text_clear_rect{};
    RECT text_copy_rect{};
    if (!GetWindowRect(text_clear_, &text_clear_rect) ||
        !GetWindowRect(text_copy_, &text_copy_rect)) {
        throw AppError("settings self-test text action buttons are missing");
    }
    if (text_clear_rect.left != text_copy_rect.left ||
        text_clear_rect.right - text_clear_rect.left !=
            text_copy_rect.right - text_copy_rect.left ||
        text_clear_rect.bottom - text_clear_rect.top != px(22, dpi_) ||
        text_copy_rect.top < text_clear_rect.bottom ||
        text_clear_rect.left < text_input_rect.left ||
        text_clear_rect.left >= text_input_rect.left + px(80, dpi_) ||
        text_clear_rect.bottom > text_input_rect.top ||
        text_copy_rect.top < text_input_rect.bottom ||
        text_copy_rect.bottom > text_output_rect.top ||
        text_copy_rect.right > text_input_rect.right) {
        throw AppError("settings self-test text action buttons lost their captions");
    }
    RECT text_capture_rect{};
    RECT text_auto_copy_rect{};
    if (!GetWindowRect(text_capture_, &text_capture_rect) ||
        !GetWindowRect(auto_copy_, &text_auto_copy_rect)) {
        throw AppError("settings self-test text translation tools are missing");
    }
    if (text_capture_rect.right - text_capture_rect.left != px(34, dpi_) ||
        text_capture_rect.bottom - text_capture_rect.top != px(34, dpi_) ||
        text_capture_rect.left >= text_input_rect.left ||
        text_capture_rect.bottom > text_input_rect.top ||
        text_of(auto_copy_) != L"译文自动复制到剪贴板" ||
        text_auto_copy_rect.left != text_capture_rect.left ||
        text_auto_copy_rect.top < text_output_rect.bottom) {
        throw AppError("settings self-test text translation tools are misaligned");
    }
    show_page(1);
    SendMessageW(translator_class_, CB_SETCURSEL, 1, 0);
    refresh_provider_fields(false);
    COMBOBOXINFO model_info{sizeof(model_info)};
    RECT model_combo_rect{};
    RECT model_edit_rect{};
    if (!GetComboBoxInfo(provider_model_, &model_info) || !model_info.hwndItem ||
        !GetWindowRect(provider_model_, &model_combo_rect) ||
        !GetWindowRect(model_info.hwndItem, &model_edit_rect)) {
        throw AppError("settings self-test could not inspect model combo edit field");
    }
    DWORD_PTR combo_edit_reference = 0;
    if (!GetWindowSubclass(model_info.hwndItem,
                           &SettingsWindow::combo_edit_proc,
                           combo_edit_subclass_id, &combo_edit_reference) ||
        combo_edit_reference != reinterpret_cast<DWORD_PTR>(this)) {
        throw AppError("settings self-test model combo edit listener is missing");
    }
    const int model_edit_top = model_edit_rect.top - model_combo_rect.top;
    if (model_edit_top < px(logical_combo_edit_offset_y + 1, dpi_)) {
        throw AppError("settings self-test model combo text remained top aligned");
    }
    align_combo_edit(provider_model_, dpi_);
    RECT realigned_model_edit_rect{};
    if (!GetWindowRect(model_info.hwndItem, &realigned_model_edit_rect) ||
        realigned_model_edit_rect.top < model_combo_rect.top ||
        realigned_model_edit_rect.bottom > model_combo_rect.bottom) {
        throw AppError("settings self-test model combo escaped its frame");
    }
    const int model_edit_width = model_edit_rect.right - model_edit_rect.left;
    const int model_edit_height = model_edit_rect.bottom - model_edit_rect.top;
    SetWindowPos(model_info.hwndItem, nullptr, 0, 0,
                 model_edit_width, model_edit_height,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
    RECT restored_model_edit_rect{};
    if (!GetWindowRect(model_info.hwndItem, &restored_model_edit_rect) ||
        restored_model_edit_rect.left < model_combo_rect.left ||
        restored_model_edit_rect.right > model_combo_rect.right ||
        restored_model_edit_rect.top < model_combo_rect.top ||
        restored_model_edit_rect.bottom > model_combo_rect.bottom) {
        throw AppError("settings self-test model combo focus reset was not repaired");
    }
    RECT test_button_rect{};
    RECT test_status_rect{};
    RECT client_bounds{};
    if (!GetWindowRect(test_button_, &test_button_rect) ||
        !GetWindowRect(test_status_, &test_status_rect) ||
        !GetClientRect(window_, &client_bounds) ||
        !MapWindowPoints(HWND_DESKTOP, window_,
                         reinterpret_cast<POINT*>(&test_status_rect), 2) ||
        (GetWindowLongPtrW(test_status_, GWL_STYLE) & SS_CENTERIMAGE) == 0 ||
        test_status_rect.left < 0 ||
        test_status_rect.right > client_bounds.right ||
        test_status_rect.bottom > client_bounds.bottom) {
        throw AppError("settings self-test connection status is not button aligned");
    }
    MSG pending_selection_clear{};
    while (PeekMessageW(&pending_selection_clear, window_,
                        clear_combo_selection_message,
                        clear_combo_selection_message, PM_REMOVE)) {}
    constexpr std::wstring_view selection_test_model = L"selection-test-model";
    SetWindowTextW(provider_model_, selection_test_model.data());
    set_combo_edit_redraw(provider_model_, false);
    SendMessageW(model_info.hwndItem, EM_SETSEL, 0, -1);
    clear_combo_edit_selection(provider_model_);
    set_combo_edit_redraw(provider_model_, true);
    PostMessageW(window_, clear_combo_selection_message,
                 reinterpret_cast<WPARAM>(provider_model_), 0);
    if (!PeekMessageW(&pending_selection_clear, window_,
                      clear_combo_selection_message,
                      clear_combo_selection_message, PM_REMOVE)) {
        throw AppError("settings self-test model selection clear was not deferred");
    }
    DispatchMessageW(&pending_selection_clear);
    DWORD selection_start = 0;
    DWORD selection_end = 0;
    SendMessageW(model_info.hwndItem, EM_GETSEL,
                 reinterpret_cast<WPARAM>(&selection_start),
                 reinterpret_cast<LPARAM>(&selection_end));
    if (selection_start != selection_end ||
        selection_end != selection_test_model.size()) {
        throw AppError("settings self-test default model remained selected");
    }
    SendMessageW(translator_class_, CB_SETCURSEL, 0, 0);
    SendMessageW(provider_, CB_SETCURSEL, 0, 0);
    refresh_provider_fields(true);
    SendMessageW(provider_, CB_SETCURSEL, 2, 0);
    refresh_provider_fields(true);
    if ((GetWindowLongPtrW(provider_key_, GWL_STYLE) & WS_VISIBLE) != 0 ||
        (GetWindowLongPtrW(provider_model_, GWL_STYLE) & WS_VISIBLE) != 0 ||
        (GetWindowLongPtrW(provider_endpoint_, GWL_STYLE) & WS_VISIBLE) != 0 ||
        (GetWindowLongPtrW(model_refresh_, GWL_STYLE) & WS_VISIBLE) != 0) {
        throw AppError("settings self-test retained AI fields for a free provider");
    }
    show_page(2);
    if ((GetWindowLongPtrW(ocr_languages_, GWL_STYLE) & WS_VISIBLE) == 0 ||
        (GetWindowLongPtrW(translation_target_, GWL_STYLE) & WS_VISIBLE) == 0) {
        throw AppError("settings self-test hid the offline OCR language list");
    }
    if (SendMessageW(translation_target_, CB_GETCOUNT, 0, 0) !=
        static_cast<LRESULT>(language_choices.size())) {
        throw AppError("settings self-test translation target list mismatch");
    }
    {
        wchar_t first_target[64]{};
        SendMessageW(translation_target_, CB_GETLBTEXT, 0,
                     reinterpret_cast<LPARAM>(first_target));
        if (std::wstring_view(first_target) != L"简体中文") {
            throw AppError("settings self-test translation target default mismatch");
        }
    }
    RECT ocr_engine_rect{};
    RECT ocr_languages_rect{};
    RECT ocr_target_rect{};
    RECT ocr_upscale_rect{};
    RECT ocr_installed_note_rect{};
    if (!GetWindowRect(ocr_engine_value_, &ocr_engine_rect) ||
        !GetWindowRect(ocr_languages_, &ocr_languages_rect) ||
        !GetWindowRect(translation_target_, &ocr_target_rect) ||
        !GetWindowRect(ocr_upscale_, &ocr_upscale_rect) ||
        !GetWindowRect(page_controls_[2][ocr_index::installed_label],
                       &ocr_installed_note_rect) ||
        ocr_languages_rect.top < ocr_engine_rect.bottom ||
        ocr_target_rect.top < ocr_languages_rect.bottom ||
        ocr_upscale_rect.top <= ocr_target_rect.bottom ||
        ocr_upscale_rect.bottom >= ocr_installed_note_rect.top ||
        !text_of(page_controls_[2][ocr_index::installed_label])
             .starts_with(L"系统已安装：")) {
        throw AppError("settings self-test OCR sections are misaligned");
    }
    show_page(3);
    layout_controls();
    if (page_controls_[3].size() != other_index::check_updates + 2 ||
        text_of(page_controls_[3][other_index::theme_label]) != L"主题" ||
        !theme_mode_ || !autostart_ ||
        text_of(autostart_) != L"开机自动启动" ||
        text_of(restart_) != L"重启程序" ||
        text_of(check_updates_) != L"检查更新" ||
        (GetWindowLongPtrW(check_updates_, GWL_STYLE) & WS_VISIBLE) == 0 ||
        (GetWindowLongPtrW(theme_mode_, GWL_STYLE) & WS_VISIBLE) == 0) {
        throw AppError("settings self-test other page rows are missing");
    }
    {
        RECT button_bounds{};
        RECT status_bounds{};
        if (!GetWindowRect(check_updates_, &button_bounds) ||
            !GetWindowRect(update_status_, &status_bounds) ||
            status_bounds.left < button_bounds.right ||
            status_bounds.top != button_bounds.top) {
            throw AppError("settings self-test update status is not beside the button");
        }
    }
    for (const HWND control : page_controls_[3]) {
        if (text_of(control) == L"配置文件") {
            throw AppError("settings self-test config file section is still present");
        }
    }
    const int content_limit = px(logical_content_limit, dpi_);
    RECT status_bounds{};
    if (!GetWindowRect(status_, &status_bounds) ||
        !MapWindowPoints(HWND_DESKTOP, window_,
                         reinterpret_cast<POINT*>(&status_bounds), 2)) {
        throw AppError("settings self-test could not measure the footer");
    }
    const int footer_top = status_bounds.top;
    for (int page = 0; page < static_cast<int>(page_controls_.size()); ++page) {
        show_page(page);
        if (page == 1) {
            SendMessageW(translator_class_, CB_SETCURSEL, 1, 0);
            refresh_provider_fields(false);
        }
        layout_controls();
        for (const HWND control : page_controls_[static_cast<std::size_t>(page)]) {
            if (!control || (GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) == 0) {
                continue;
            }
            RECT bounds{};
            if (!GetWindowRect(control, &bounds) ||
                !MapWindowPoints(HWND_DESKTOP, window_,
                                 reinterpret_cast<POINT*>(&bounds), 2)) {
                throw AppError("settings self-test could not measure page controls");
            }
            if (bounds.bottom > std::min(content_limit,
                                         footer_top - px(logical_status_margin, dpi_))) {
                throw AppError("settings self-test page content overlaps the footer");
            }
        }
        if (page == 1) {
            SendMessageW(translator_class_, CB_SETCURSEL, 0, 0);
            refresh_provider_fields(false);
        }
    }
    DestroyWindow(window_);
    window_ = nullptr;
}


}  // namespace minitrans
