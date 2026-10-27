#include "settings_window.hpp"
#include "settings_window_internal.hpp"

#include "provider_key_dialog.hpp"
#include "settings_ai_dialog.hpp"
#include "self_test_support.hpp"
#include "version.hpp"

namespace minitrans {

using namespace settings_internal;

void SettingsWindow::self_test(HWND owner) {
    if (window_) throw AppError("settings self-test window already exists");
    create_window(owner);
    if (!window_ || !capture_hotkey_ || !toggle_hotkey_ || !text_hotkey_ ||
        !provider_ || !class_traditional_ || !class_ai_ || !ai_list_ ||
        !ai_add_ || !ai_list_caption_ || !provider_hint_ || !test_button_ ||
        !test_status_ || !ocr_engine_value_ || !translation_target_ ||
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
    // The 类别 switch replaced the drop-down, so the two halves are checked
    // as controls rather than as combo rows. The shared list caption starts
    // on the endpoint branch; the traditional branch renames it to 接口.
    if (text_of(class_traditional_) != L"传统翻译" ||
        text_of(class_ai_) != L"大模型翻译" ||
        text_of(ai_add_) != L"添加") {
        throw AppError("settings self-test translation switch captions changed");
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
    if (SendMessageW(text_target_, CB_GETCOUNT, 0, 0) != 9) {
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
    RECT text_target_rect{};
    if (!GetWindowRect(text_capture_, &text_capture_rect) ||
        !GetWindowRect(auto_copy_, &text_auto_copy_rect) ||
        !GetWindowRect(text_target_, &text_target_rect)) {
        throw AppError("settings self-test text translation tools are missing");
    }
    if (text_capture_rect.right - text_capture_rect.left != px(34, dpi_) ||
        text_capture_rect.bottom - text_capture_rect.top != px(34, dpi_) ||
        text_capture_rect.top != text_target_rect.top ||
        text_capture_rect.left <= text_target_rect.right ||
        text_capture_rect.bottom > text_input_rect.top ||
        text_of(auto_copy_) != L"译文自动复制到剪贴板" ||
        text_auto_copy_rect.left >= text_input_rect.left ||
        text_auto_copy_rect.top < text_output_rect.bottom) {
        throw AppError("settings self-test text translation tools are misaligned");
    }
    show_page(1);
    // Two endpoints, one of them enabled: everything below drives the page the
    // way a user would, so the assertions check behaviour rather than pixels.
    const auto seed_endpoints = [&] {
        config_.set_json(L"translator.ai_profiles",
                         JsonArray::Parse(
                             L"[{\"name\":\"甲\",\"protocol\":\"openai_chat\","
                             L"\"base_url\":\"https://a.test/v1\",\"key\":\"\","
                             L"\"model\":\"m\",\"enabled\":true},"
                             L"{\"name\":\"乙\",\"protocol\":\"openai_chat\","
                             L"\"base_url\":\"\",\"key\":\"\",\"model\":\"\","
                             L"\"enabled\":false}]"));
        populate_ai_list();
    };
    const auto row_rect = [&](int index) {
        RECT bounds{};
        SendMessageW(ai_list_, LB_GETITEMRECT, static_cast<WPARAM>(index),
                     reinterpret_cast<LPARAM>(&bounds));
        return bounds;
    };
    const auto button_center = [&](int index, settings_ai_list::Button wanted) {
        const RECT row = row_rect(index);
        for (const auto& [action, bounds] :
             settings_ai_list::buttons(row, dpi_, nullptr)) {
            if (action == wanted) {
                return POINT{(bounds.left + bounds.right) / 2,
                             (bounds.top + bounds.bottom) / 2};
            }
        }
        return POINT{row.left, row.top};
    };
    const auto visible = [](HWND control) {
        return control && (GetWindowLongPtrW(control, GWL_STYLE) & WS_VISIBLE) != 0;
    };
    set_translation_class(true);
    seed_endpoints();
    refresh_provider_fields();
    if (SendMessageW(ai_list_, LB_GETCOUNT, 0, 0) != 2 ||
        !visible(ai_list_) || !visible(ai_add_) || visible(ai_list_caption_) ||
        visible(provider_) || visible(provider_key_) || visible(test_button_)) {
        throw AppError("settings self-test large-model branch is incomplete");
    }
    // 添加 rides beside the class switch and only works on this branch.
    if (!IsWindowEnabled(ai_add_)) {
        throw AppError("settings self-test 添加 stayed disabled on the endpoint branch");
    }
    // The list box item height is the row geometry the page reserves and the
    // rows are painted and hit-tested with, so the three must be one number.
    const int item_height = static_cast<int>(
        SendMessageW(ai_list_, LB_GETITEMHEIGHT, 0, 0));
    const RECT first_row = row_rect(0);
    RECT list_client{};
    GetClientRect(ai_list_, &list_client);
    const auto row_actions = settings_ai_list::buttons(first_row, dpi_, nullptr);
    // Buttons hug the right edge with a 4px margin, per the shared layout.
    if (item_height != px(settings_ai_list::logical_row_height(), dpi_) ||
        first_row.bottom - first_row.top != item_height ||
        row_actions.size() != 3 || first_row.left < list_client.left ||
        first_row.right > list_client.right ||
        row_actions[2].second.right > first_row.right - px(4, dpi_)) {
        throw AppError("settings self-test endpoint rows lost their geometry");
    }
    for (std::size_t index = 0; index < row_actions.size(); ++index) {
        const auto& bounds = row_actions[index].second;
        const POINT center{(bounds.left + bounds.right) / 2,
                           (bounds.top + bounds.bottom) / 2};
        if (settings_ai_list::hit_test(first_row, center, dpi_) !=
            row_actions[index].first) {
            throw AppError("settings self-test endpoint row hit test is off");
        }
    // The list branch carries no note under the rows, so the card hugs the band:
    // its bottom is the last row plus one card padding and nothing else.
    RECT list_bounds{};
    if (!GetWindowRect(ai_list_, &list_bounds) ||
        !MapWindowPoints(HWND_DESKTOP, window_,
                         reinterpret_cast<POINT*>(&list_bounds), 2)) {
        throw AppError("settings self-test could not measure the endpoint list");
    }
    if (visible(provider_hint_) ||
        translation_.card_bottom - list_bounds.bottom !=
            px(logical_card_padding, dpi_)) {
        throw AppError("settings self-test endpoint list kept a note under the rows");
    }
    }
    // One enabled endpoint is the rule the pipeline depends on.
    auto stored = read_profiles();
    if (stored.size() != 2 || !stored[0].enabled || stored[1].enabled) {
        throw AppError("settings self-test seeded enable flag did not survive");
    }
    // 停用 on the enabled row turns the class off, and the note stays hidden:
    // the list branch shows no explanation under the rows.
    on_ai_list_click(button_center(0, settings_ai_list::Button::enable));
    stored = read_profiles();
    if (stored[0].enabled || stored[1].enabled || visible(provider_hint_)) {
        throw AppError("settings self-test 停用 did not clear the switch");
    }
    // An endpoint without an address or model cannot be enabled; the row stays
    // off and the page tells the user to edit it first.
    on_ai_list_click(button_center(1, settings_ai_list::Button::enable));
    stored = read_profiles();
    if (stored[1].enabled) {
        throw AppError("settings self-test enabled an incomplete endpoint");
    }
    // 启用 on the configured row picks it up, and only one row stays enabled.
    on_ai_list_click(button_center(0, settings_ai_list::Button::enable));
    stored = read_profiles();
    if (!stored[0].enabled || stored[1].enabled) {
        throw AppError("settings self-test enable did not pick the configured row");
    }
    // The row under the cursor is tracked so only that row highlights.
    update_ai_hover(ai_list_, button_center(0, settings_ai_list::Button::edit));
    if (ai_list_hover_ != 0 || ai_hover_button_ != settings_ai_list::Button::edit) {
        throw AppError("settings self-test endpoint hover missed the button");
    }
    clear_ai_hover(ai_list_);
    if (ai_list_hover_ != -1 || ai_hover_button_ != settings_ai_list::Button::none) {
        throw AppError("settings self-test endpoint hover did not reset");
    }
    // Deleting needs a second click, and the timeout drops the pending request.
    const POINT remove_first = button_center(0, settings_ai_list::Button::remove);
    on_ai_list_click(remove_first);
    if (pending_delete_ != L"甲" || ai_rows_.size() != 2 ||
        delete_deadline_ <= GetTickCount64()) {
        throw AppError("settings self-test delete did not ask for confirmation");
    }
    on_ai_list_click(remove_first);
    if (ai_rows_.size() != 1 || !pending_delete_.empty() || delete_deadline_ != 0) {
        throw AppError("settings self-test confirmed delete kept the wrong row count");
    }
    handle_message(WM_TIMER, delete_confirm_timer, 0);
    seed_endpoints();
    refresh_provider_fields();
    handle_message(WM_TIMER, delete_confirm_timer, 0);
    on_ai_list_click(remove_first);
    handle_message(WM_TIMER, delete_confirm_timer, 0);
    if (!pending_delete_.empty() || ai_rows_.size() != 2) {
        throw AppError("settings self-test delete confirmation did not time out");
    }
    // The card wraps every row of the branch it belongs to.
    if (translation_.card_top > px(logical_content_top, dpi_) ||
        !large_model()) {
        throw AppError("settings self-test translation geometry lost its branch");
    }
    SettingsAiDialog::self_test(instance_, window_);
    ProviderKeyDialog::self_test(instance_, window_);
    // The traditional branch is the same list control now: one toggle per
    // key-free provider, and DeepL with 编辑 that opens the key dialog.
    set_translation_class(false);
    // 添加 stays visible on both branches now, disabled while the
    // traditional providers are showing.
    if (visible(provider_) || visible(test_button_) || !visible(ai_add_) ||
        IsWindowEnabled(ai_add_) ||
        !visible(ai_list_) || text_of(ai_list_caption_) != L"接口") {
        throw AppError("settings self-test traditional branch is incomplete");
    }
    if (traditional_rows_.size() != providers.size() - 1) {
        throw AppError("settings self-test traditional list lost a provider");
    }
    for (const auto& row : traditional_rows_) {
        const bool is_deepl = row.name == L"DeepL";
        if (row.actions != (is_deepl
                                ? settings_ai_list::Row::Actions::toggle_edit
                                : settings_ai_list::Row::Actions::toggle)) {
            throw AppError("settings self-test traditional row actions are wrong");
        }
    }
    // The DeepL toggle drives the key dialog; skip the modal part and check
    // the enable path by pointing the provider at it directly.
    loaded_provider_ = L"deepl";
    populate_traditional_list();
    if (!traditional_rows_.back().enabled) {
        throw AppError("settings self-test DeepL row did not enable");
    }
    if (translation_.card_top > px(logical_content_top, dpi_)) {
        throw AppError("settings self-test traditional geometry lost the card");
    }
    config_.set_json(L"translator.ai_profiles", JsonArray::Parse(L"[]"));
    populate_ai_list();
    refresh_provider_fields();
    set_translation_class(true);
    if (visible(ai_list_) || !visible(ai_add_)) {
        throw AppError("settings self-test empty endpoint list still drew a card");
    }
    set_translation_class(false);
    refresh_provider_fields();
    if (loaded_provider_ == L"ai") {
        throw AppError("settings self-test could not switch back to traditional");
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
    // 主题 is one segmented row of three chips now; the rest of the page keeps
    // its rows and buttons.
    if (page_controls_[3].size() != other_index::version_label + 1 ||
        text_of(page_controls_[3][other_index::theme_label]) != L"主题" ||
        !theme_mode_ || !theme_segment_[0] || !theme_segment_[1] || !autostart_ ||
        text_of(theme_mode_) != theme_modes[0] ||
        text_of(theme_segment_[0]) != theme_modes[1] ||
        text_of(theme_segment_[1]) != theme_modes[2] ||
        text_of(autostart_) != L"开机自动启动" ||
        text_of(diagnostics_) != L"诊断信息" ||
        text_of(check_updates_) != L"检查更新" ||
        (GetWindowLongPtrW(check_updates_, GWL_STYLE) & WS_VISIBLE) == 0 ||
        (GetWindowLongPtrW(theme_mode_, GWL_STYLE) & WS_VISIBLE) == 0 ||
        (GetWindowLongPtrW(theme_segment_[1], GWL_STYLE) & WS_VISIBLE) == 0) {
        throw AppError("settings self-test other page rows are missing");
    }
    // The version line must carry the build's own number, not a literal.
    if (!version_label_ ||
        text_of(version_label_) != L"版本 " + std::wstring(native_version) ||
        (GetWindowLongPtrW(version_label_, GWL_STYLE) & WS_VISIBLE) == 0) {
        throw AppError("settings self-test version line is missing or stale");
    }
    {
        // The three chips sit edge to edge at the same height, right of the
        // caption column and inside the card's inner width. client_bounds at
        // function scope already holds the client rect.
        RECT first_chip{};
        RECT middle_chip{};
        RECT chip_client{};
        if (!GetWindowRect(theme_mode_, &first_chip) ||
            !GetWindowRect(theme_segment_[0], &middle_chip) ||
            !GetClientRect(window_, &chip_client)) {
            throw AppError("settings self-test theme chips are unmeasurable");
        }
        MapWindowPoints(HWND_DESKTOP, window_,
                        reinterpret_cast<POINT*>(&first_chip), 2);
        MapWindowPoints(HWND_DESKTOP, window_,
                        reinterpret_cast<POINT*>(&middle_chip), 2);
        const int inner_right = chip_client.right -
            px(logical_page_margin + 18, dpi_);
        // Adjacent chips share the row with a small breathing gap.
        if (first_chip.top != middle_chip.top ||
            middle_chip.left <= first_chip.right ||
            middle_chip.left - first_chip.right > px(24, dpi_) ||
            middle_chip.right > inner_right) {
            throw AppError("settings self-test theme chips are misaligned");
        }
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
    // Every branch of the translation page is walked, including a list long
    // enough to reach its row cap: the tallest content must still stop above
    // the footer instead of scrolling into it.
    const auto measure_page = [&](int page) {
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
        // The card belongs to the layout pass, so the page is also checked
        // against the geometry the paint pass will use.
        if (page == 1) {
            const int limit = std::min(content_limit,
                                       footer_top - px(logical_status_margin, dpi_));
            if (translation_.card_bottom > limit ||
                translation_.card_bottom <= translation_.card_top) {
                throw AppError("settings self-test translation card overlaps the footer");
            }
        }
    };
    for (int page = 0; page < static_cast<int>(page_controls_.size()); ++page) {
        show_page(page);
        if (page == 1) {
            set_translation_class(false);
            measure_page(page);
            set_translation_class(true);
            measure_page(page);
            const std::wstring many = [&] {
                std::wstring json = L"[";
                for (int row = 0; row <= logical_ai_list_max_rows; ++row) {
                    if (row > 0) json += L',';
                    json += L"{\"name\":\"接口 " + std::to_wstring(row + 1) +
                            L"\",\"protocol\":\"openai_chat\","
                            L"\"base_url\":\"https://a.test/v1\",\"key\":\"\","
                            L"\"model\":\"m\",\"enabled\":" +
                            std::wstring(row == 0 ? L"true" : L"false") + L"}";
                }
                return json + L"]";
            }();
            config_.set_json(L"translator.ai_profiles", JsonArray::Parse(many.c_str()));
            populate_ai_list();
            refresh_provider_fields();
            measure_page(page);
            if (ai_list_item_count() <= logical_ai_list_max_rows ||
                translation_.separator_count != 1) {
                throw AppError("settings self-test long endpoint list is not capped");
            }
            set_translation_class(false);
        } else {
            measure_page(page);
        }
    }
    config_.set_json(L"translator.ai_profiles", JsonArray::Parse(L"[]"));
    populate_ai_list();
    // Switching pages repaints every self-drawn control. The window keeps its
    // theme resources alive between switches, so a loop like this must not grow
    // GDI/USER objects or private bytes.
    self_test_support::settle_self_test_ui();
    // Six batches are the minimum resource_series_stable() accepts: three
    // warm-up batches plus three measured ones. Three passes over every page
    // per batch still repaints each self-drawn control often enough for a
    // per-switch leak to show up.
    std::array<self_test_support::ProcessResourceSnapshot, 6> resource_samples{};
    resource_samples[0] = self_test_support::process_resources();
    for (std::size_t batch = 1; batch < resource_samples.size(); ++batch) {
        for (int pass = 0; pass < 3; ++pass) {
            for (int page = 0; page < static_cast<int>(page_controls_.size()); ++page) {
                show_page(page);
                layout_controls();
                RedrawWindow(window_, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
            }
        }
        resource_samples[batch] = self_test_support::process_resources();
    }
    if (!self_test_support::resource_series_stable(resource_samples)) {
        throw AppError("settings window self-test leaked process resources" +
                       self_test_support::resource_change(resource_samples.front(),
                                                          resource_samples.back()));
    }
    DestroyWindow(window_);
    window_ = nullptr;
}


}  // namespace minitrans
