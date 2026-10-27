#include "settings_ai_dialog.hpp"

#include "connection_test.hpp"
#include "settings_catalog.hpp"
#include "settings_window_internal.hpp"
#include "theme.hpp"
#include "translator.hpp"
#include "ui_controls.hpp"
#include "ui_draw.hpp"
#include "util.hpp"

#include <commctrl.h>
#include <uxtheme.h>
#include <shellscalingapi.h>
#include <windowsx.h>
#include <winrt/Windows.Data.Json.h>

#include <algorithm>

namespace minitrans {

using namespace settings_internal;
using winrt::Windows::Data::Json::JsonObject;

namespace {

constexpr wchar_t dlg_class_name[] = L"MiniTranslator.Native.SettingsAiDialog.v1";
constexpr int id_name = 7101;
// The segmented 协议 row: one id per wire format, replacing the old combo id.
constexpr int id_protocol_chat = 7102;
constexpr int id_protocol_responses = 7109;
constexpr int id_protocol_anthropic = 7110;
constexpr int id_endpoint = 7103;
constexpr int id_key = 7104;
constexpr int id_model = 7105;
constexpr int id_refresh_models = 7106;
constexpr int dlg_id_test = 7107;
constexpr int id_save = 7108;
constexpr UINT_PTR dlg_edit_subclass_id = 1;
constexpr UINT_PTR dlg_combo_subclass_id = 2;
constexpr UINT_PTR dlg_combo_edit_subclass_id = 3;

constexpr int logical_width = 460;
// Five labelled fields (the model row also carries its two buttons), the status
// line and the footer need this much client height before the card would reach
// the save/cancel row.
constexpr int logical_height = 504;
constexpr int logical_margin = 18;
constexpr int dlg_logical_card_padding = 14;
constexpr int logical_label_height = 18;
constexpr int logical_field_height = 34;
constexpr int logical_button_height = 34;
constexpr int logical_button_width = 88;
constexpr int logical_gap = 6;
constexpr int dlg_logical_status_height = 18;
// Height of the in-card title band that replaced the system caption.
constexpr int dlg_logical_title_height = 26;

constexpr std::array<const wchar_t*, 5> field_labels{
    L"名称", L"协议", L"Base URL", L"API Key", L"模型",
};

}  // namespace

SettingsAiDialog::SettingsAiDialog(HINSTANCE instance, HWND owner,
                                   const settings_profiles::Profile& current,
                                   std::wstring_view original_name,
                                   std::wstring_view suggested_name)
    : ModalDialog(instance, owner),
      original_(current),
      original_name_(original_name.empty() ? std::wstring(suggested_name)
                                           : std::wstring(original_name)) {
    if (original_.name.empty()) original_.name = original_name_;
    if (original_.protocol.empty()) original_.protocol = L"openai_chat";
}

SettingsAiDialog::~SettingsAiDialog() {
    cancel_model_refresh();
    if (test_thread_.joinable()) {
        test_thread_.request_stop();
        test_thread_.join();
    }
    discard_connection_test_results(window_);
    discard_model_refresh_results(window_);
    if (small_font_) DeleteObject(small_font_);
}

SettingsAiDialog::Result SettingsAiDialog::edit(
    HINSTANCE instance, HWND owner, const settings_profiles::Profile& current,
    std::wstring_view original_name, std::wstring_view suggested_name) {
    SettingsAiDialog dialog(instance, owner, current, original_name,
                            suggested_name);
    dialog.create_popup(dlg_class_name, L"大模型翻译接口", logical_width,
                        logical_height);
    if (!dialog.window_) throw AppError("无法打开大模型接口编辑窗口");
    dialog.create_controls();
    // Modal without disabling the owner: the settings window keeps painting
    // while input is consumed here until the dialog closes.
    dialog.run_modal();
    return dialog.result_;
}

void SettingsAiDialog::self_test(HINSTANCE instance, HWND owner) {
    settings_profiles::Profile profile;
    profile.name = L"self-test";
    profile.protocol = L"openai_chat";
    profile.base_url = L"https://example.test/v1";
    profile.model = L"self-test-model";
    SettingsAiDialog dialog(instance, owner, profile, profile.name, L"");
    dialog.create_popup(dlg_class_name, L"大模型翻译接口", logical_width,
                        logical_height);
    if (!dialog.window_) throw AppError("endpoint dialog self-test window failed");
    dialog.create_controls();
    if (!dialog.name_ || !dialog.endpoint_ || !dialog.key_ ||
        !dialog.model_ || !dialog.model_refresh_ || !dialog.test_ || !dialog.save_ ||
        !dialog.protocol_segment_[0] || !dialog.protocol_segment_[1] ||
        !dialog.protocol_segment_[2]) {
        throw AppError("endpoint dialog self-test did not create required controls");
    }
    if (dialog.protocol_segment() != 0) {
        throw AppError("endpoint dialog self-test protocol default mismatch");
    }
    if (text_of(dialog.name_) != L"self-test" ||
        text_of(dialog.endpoint_) != L"https://example.test/v1" ||
        text_of(dialog.model_) != L"self-test-model") {
        throw AppError("endpoint dialog self-test did not load the endpoint");
    }
    const auto placement = dialog.placement_;
    for (std::size_t index = 0; index < field_labels.size(); ++index) {
        const auto& shell = placement.shells[index];
        if (shell.right - shell.left < 1 || shell.bottom - shell.top < 1 ||
            shell.right > placement.card.right || shell.left < placement.card.left) {
            throw AppError("endpoint dialog self-test field escapes the card");
        }
        if (index + 1 < field_labels.size() &&
            placement.shells[index + 1].top <= shell.bottom) {
            throw AppError("endpoint dialog self-test fields overlap");
        }
    }
    // The model row carries 获取模型 / 测试连接 / 保存 inside the card's
    // right padding; no footer and no close chip exist any more.
    if (placement.save.right > placement.shells.back().right ||
        placement.test.right > placement.save.left ||
        placement.refresh.right > placement.test.left ||
        placement.save.top < placement.shells.back().top ||
        placement.save.bottom > placement.shells.back().bottom ||
        placement.card.bottom < placement.save.bottom) {
        throw AppError("endpoint dialog self-test action row is misplaced");
    }
    settings_profiles::Profile collected;
    std::wstring error;
    if (!dialog.collect(collected, error) || collected.name != L"self-test" ||
        collected.model != L"self-test-model" || !error.empty()) {
        throw AppError("endpoint dialog self-test could not read its fields");
    }
    SetWindowTextW(dialog.endpoint_, L"");
    if (dialog.collect(collected, error) || error.empty()) {
        throw AppError("endpoint dialog self-test accepted an empty base URL");
    }
    // The segmented protocol row decides the stored wire format.
    dialog.set_protocol_segment(2);
    dialog.collect(collected, error);
    if (collected.protocol != ai_protocols[2].first) {
        throw AppError("endpoint dialog self-test protocol switch did not stick");
    }
    dialog.set_protocol_segment(1);
    dialog.collect(collected, error);
    if (collected.protocol != ai_protocols[1].first) {
        throw AppError("endpoint dialog self-test protocol switch lost a chip");
    }
    DestroyWindow(dialog.window_);
    dialog.window_ = nullptr;
}


void SettingsAiDialog::create_controls() {
    create_theme_resources();
    const auto make = [&](DWORD extra, const wchar_t* text, int identifier) {
        HWND control = CreateWindowExW(
            0, L"EDIT", text,
            WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL | extra | WS_TABSTOP,
            0, 0, 1, 1, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)), instance_,
            nullptr);
        if (!control) throw_last_error("create endpoint field");
        set_font(control, font_);
        // The dialog paints each rounded frame, so the native edit must not
        // grow the classic square border.
        SetWindowTheme(control, L"", L"");
        SetWindowLongPtrW(control, GWL_STYLE,
                          GetWindowLongPtrW(control, GWL_STYLE) &
                              ~static_cast<LONG_PTR>(WS_BORDER));
        const auto extended = GetWindowLongPtrW(control, GWL_EXSTYLE);
        SetWindowLongPtrW(control, GWL_EXSTYLE,
                          extended & ~static_cast<LONG_PTR>(WS_EX_CLIENTEDGE |
                                                          WS_EX_STATICEDGE));
        SetWindowPos(control, nullptr, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                         SWP_FRAMECHANGED);
        SendMessageW(control, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(px(9, dpi_), px(9, dpi_)));
        SetWindowSubclass(control, &SettingsAiDialog::edit_proc, dlg_edit_subclass_id,
                          reinterpret_cast<DWORD_PTR>(this));
        return control;
    };

    name_ = make(0, original_.name.c_str(), id_name);
    endpoint_ = make(0, original_.base_url.c_str(), id_endpoint);
    key_ = make(ES_PASSWORD, original_.key.c_str(), id_key);
    model_ = CreateWindowExW(
        0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | CBS_DROPDOWN | CBS_AUTOHSCROLL |
            CBS_OWNERDRAWFIXED | CBS_HASSTRINGS | WS_VSCROLL | WS_TABSTOP,
        0, 0, 1, 1, window_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id_model)), instance_, nullptr);
    if (!model_) throw_last_error("create endpoint model combo");
    const auto make_button = [&](const wchar_t* text, int identifier) {
        HWND control = CreateWindowExW(
            0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP,
            0, 0, 1, 1, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)), instance_,
            nullptr);
        if (!control) throw_last_error("create endpoint button");
        set_font(control, font_);
        return control;
    };
    model_refresh_ = make_button(L"获取模型", id_refresh_models);
    test_ = make_button(L"测试连接", dlg_id_test);
    save_ = make_button(L"保存", id_save);

    // 协议 is one segmented row of three chips instead of a drop-down.
    constexpr std::array<int, 3> protocol_ids{
        id_protocol_chat, id_protocol_responses, id_protocol_anthropic,
    };
    for (std::size_t index = 0; index < ai_protocol_segments.size(); ++index) {
        protocol_segment_[index] = make_button(
            ai_protocol_segments[index].data(), protocol_ids[index]);
    }

    for (HWND combo : {model_}) {
        set_font(combo, font_);
        theme::apply_control_theme(combo);
        COMBOBOXINFO box{sizeof(box)};
        if (GetComboBoxInfo(combo, &box) && box.hwndItem &&
            box.hwndItem != combo) {
            set_font(box.hwndItem, font_);
            theme::apply_control_theme(box.hwndItem);
            SendMessageW(box.hwndItem, EM_SETMARGINS,
                         EC_LEFTMARGIN | EC_RIGHTMARGIN,
                         MAKELPARAM(px(9, dpi_), px(9, dpi_)));
            SetWindowSubclass(box.hwndItem, &SettingsAiDialog::combo_edit_proc,
                              dlg_combo_edit_subclass_id,
                              reinterpret_cast<DWORD_PTR>(this));
        }
        SetWindowSubclass(combo, &SettingsAiDialog::combo_proc, dlg_combo_subclass_id,
                          reinterpret_cast<DWORD_PTR>(this));
    }
    if (!original_.model.empty()) {
        SendMessageW(model_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(original_.model.c_str()));
        SendMessageW(model_, CB_SETCURSEL, 0, 0);
    }
    loading_ = true;
    int protocol_index = 0;
    for (std::size_t index = 0; index < ai_protocols.size(); ++index) {
        if (ai_protocols[index].first == original_.protocol) {
            protocol_index = static_cast<int>(index);
        }
    }
    set_protocol_segment(protocol_index);
    loading_ = false;
    SendMessageW(model_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"点「获取模型」拉取可用模型"));
    SendMessageW(endpoint_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"https://api.deepseek.com/v1"));
    SendMessageW(key_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"sk-..."));
    layout_controls();
}

void SettingsAiDialog::create_theme_resources() {
    ModalDialog::create_theme_resources();
    // The footer status and the protocol hints use a smaller face than the
    // fields, so this dialog keeps one extra font next to the shared set.
    if (small_font_) DeleteObject(small_font_);
    small_font_ = CreateFontW(-px(theme::font_size_small, dpi_), 0, 0, 0, FW_NORMAL,
                              FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH,
                              theme::font_family.c_str());
}

SettingsAiDialog::Placement SettingsAiDialog::place() const noexcept {
    const int margin = px(logical_margin, dpi_);
    const int padding = px(dlg_logical_card_padding, dpi_);
    RECT client{};
    GetClientRect(window_, &client);
    Placement out;
    out.card = {margin, margin, client.right - margin, margin};
    int y = out.card.top + padding;
    const int label_height = px(logical_label_height, dpi_);
    const int field_height = px(logical_field_height, dpi_);
    const int button_height = px(logical_button_height, dpi_);
    for (std::size_t index = 0; index < field_labels.size(); ++index) {
        // Every field keeps its caption again, 协议 included.
        out.labels[index] = {out.card.left + padding, y, out.card.right - padding,
                             y + label_height};
        y += label_height + px(4, dpi_);
        // The model row carries its buttons, so its shell spans combo plus the
        // 「获取模型 / 测试连接 / 保存」 row that sits under it.
        const int row_height = index == field_labels.size() - 1
            ? field_height + px(logical_gap, dpi_) + button_height
            : field_height;
        out.shells[index] = {out.card.left + padding, y, out.card.right - padding,
                             y + row_height};
        y += row_height + px(12, dpi_);
    }
    out.status = {out.card.left + padding, y, out.card.right - padding,
                  y + px(dlg_logical_status_height, dpi_)};
    out.card.bottom = out.status.bottom + padding;
    const int button_width = px(logical_button_width, dpi_);
    const int gap = px(logical_gap, dpi_);
    // 获取模型 / 测试连接 / 保存 share one row under the model combo, with
    // 保存 right of 测试连接 as the primary action; the old footer is gone
    // with the caption bar.
    const int row_bottom = out.shells.back().bottom;
    out.save = {out.card.right - padding - button_width,
                row_bottom - button_height, out.card.right - padding,
                row_bottom};
    out.test = {out.save.left - gap - button_width, out.save.top,
                out.save.left - gap, out.save.bottom};
    out.refresh = {out.test.left - gap - button_width, out.test.top,
                   out.test.left - gap, out.test.bottom};
    out.cancel = out.save;
    return out;
}

void SettingsAiDialog::layout_controls() {
    if (!window_) return;
    placement_ = place();
    const auto place_edit = [&](HWND edit, const RECT& shell, int height) {
        if (!edit) return;
        const int inset = std::max(1, px(logical_edit_inset_x, dpi_));
        const int top = shell.top + std::max(1, px(logical_edit_inset_top, dpi_));
        const int width = std::max(1, static_cast<int>(shell.right - shell.left) -
                                          inset * 2);
        MoveWindow(edit, static_cast<int>(shell.left) + inset, top, width,
                   std::max(1, height), TRUE);
    };
    const int field_height = px(logical_field_height, dpi_);
    const int well_height = field_height - std::max(1, px(logical_edit_inset_top, dpi_)) -
                            std::max(1, px(logical_edit_inset_bottom, dpi_));
    place_edit(name_, placement_.shells[0], well_height);
    place_edit(endpoint_, placement_.shells[2], well_height);
    place_edit(key_, placement_.shells[3], well_height);
    // 协议 chips split the row evenly with a visible gap between them, so
    // each chip reads as its own button; together they span the full band.
    {
        const auto& shell = placement_.shells[1];
        const int gap = px(4, dpi_);
        const int height = std::max(1, static_cast<int>(shell.bottom -
                                                         shell.top));
        const int count = static_cast<int>(ai_protocol_segments.size());
        const int width =
            std::max(1, (static_cast<int>(shell.right - shell.left) -
                         gap * (count - 1)) / count);
        int x = static_cast<int>(shell.left);
        for (int chip = 0; chip < count; ++chip) {
            MoveWindow(protocol_segment_[static_cast<std::size_t>(chip)], x,
                       static_cast<int>(shell.top), width, height, TRUE);
            x += width + gap;
        }
    }
    const auto& model_shell = placement_.shells[4];
    MoveWindow(model_, static_cast<int>(model_shell.left),
               static_cast<int>(model_shell.top),
               std::max(1, static_cast<int>(model_shell.right - model_shell.left)),
               field_height, TRUE);
    align_combo_edit(model_, dpi_);
    const int button_top = static_cast<int>(model_shell.bottom) -
                           px(logical_button_height, dpi_);
    const int button_width = px(logical_button_width, dpi_);
    const int gap = px(logical_gap, dpi_);
    MoveWindow(save_,
               static_cast<int>(model_shell.right) - button_width,
               button_top, button_width, px(logical_button_height, dpi_), TRUE);
    MoveWindow(test_,
               static_cast<int>(model_shell.right) - button_width * 2 - gap,
               button_top, button_width, px(logical_button_height, dpi_), TRUE);
    MoveWindow(model_refresh_,
               static_cast<int>(model_shell.right) - button_width * 3 - gap * 2,
               button_top, button_width, px(logical_button_height, dpi_), TRUE);
    InvalidateRect(window_, nullptr, TRUE);
}

bool SettingsAiDialog::collect(settings_profiles::Profile& profile,
                               std::wstring& error) const {
    // 保存 needs the model filled in; the fetch and the connect test only
    // need to know which endpoint to talk to.
    if (!collect_endpoint(profile, error)) return false;
    profile.model = trim(text_of(model_));
    if (profile.model.empty()) {
        error = L"请填写或选择模型";
        return false;
    }
    error.clear();
    return true;
}

bool SettingsAiDialog::collect_endpoint(settings_profiles::Profile& profile,
                                        std::wstring& error) const {
    profile = {};
    profile.name = trim(text_of(name_));
    const int protocol = std::clamp(protocol_segment(), 0,
                                    static_cast<int>(ai_protocols.size()) - 1);
    profile.protocol = std::wstring(ai_protocols[static_cast<std::size_t>(protocol)].first);
    profile.base_url = trim(text_of(endpoint_));
    profile.key = trim(text_of(key_));
    if (profile.name.empty()) {
        error = L"请填写接口名称";
        return false;
    }
    if (profile.base_url.empty()) {
        error = L"请填写 Base URL";
        return false;
    }
    error.clear();
    return true;
}

int SettingsAiDialog::protocol_segment() const noexcept {
    for (std::size_t chip = 0; chip < protocol_segment_.size(); ++chip) {
        if (protocol_segment_[chip] &&
            GetPropW(protocol_segment_[chip], L"MiniTranslator.SegmentOn")) {
            return static_cast<int>(chip);
        }
    }
    return 0;
}

void SettingsAiDialog::set_protocol_segment(int index) {
    for (std::size_t chip = 0; chip < protocol_segment_.size(); ++chip) {
        if (!protocol_segment_[chip]) continue;
        if (static_cast<int>(chip) == index) {
            SetPropW(protocol_segment_[chip], L"MiniTranslator.SegmentOn",
                     reinterpret_cast<HANDLE>(1));
        } else {
            RemovePropW(protocol_segment_[chip], L"MiniTranslator.SegmentOn");
        }
        InvalidateRect(protocol_segment_[chip], nullptr, FALSE);
    }
}

void SettingsAiDialog::set_status(std::wstring_view message, bool error) {
    status_error_ = error;
    status_text_ = std::wstring(message);
    InvalidateRect(window_, nullptr, FALSE);
}

void SettingsAiDialog::start_model_refresh() {
    if (model_thread_.joinable()) return;
    // Fetching the list is exactly the step that happens before a model is
    // known, so the model field is not required here.
    settings_profiles::Profile profile;
    std::wstring error;
    if (!collect_endpoint(profile, error)) {
        set_status(error, true);
        return;
    }
    const JsonObject options = settings_profiles::to_options(profile);
    const auto generation = ++model_generation_;
    model_refreshing_ = true;
    test_pending_ = true;
    test_success_ = false;
    EnableWindow(model_refresh_, FALSE);
    set_status(L"正在拉取模型列表…", false);
    const HWND target = window_;
    model_thread_ = std::jthread(
        [target, generation, options](std::stop_token stop) noexcept {
            try {
                auto result = std::make_unique<ModelListResult>();
                result->generation = generation;
                try {
                    WinrtApartment apartment(winrt::apartment_type::multi_threaded);
                    TranslatorService service;
                    result->models = service.list_models(options, stop);
                    result->cancelled = stop.stop_requested();
                } catch (const std::exception& inner) {
                    result->cancelled = stop.stop_requested();
                    if (!result->cancelled) {
                        result->error = redact_sensitive(utf8_to_wide(inner.what()));
                    }
                } catch (...) {
                    result->cancelled = stop.stop_requested();
                    if (!result->cancelled) {
                        result->error = L"拉取模型列表时遇到未知错误";
                    }
                }
                auto* raw = result.release();
                if (!PostMessageW(target, models_completed_message, 0,
                                  reinterpret_cast<LPARAM>(raw))) {
                    delete raw;
                }
            } catch (...) {
                // A background failure must never terminate the tray process.
            }
        });
}

void SettingsAiDialog::finish_model_refresh(ModelListResult result) {
    if (model_thread_.joinable()) model_thread_.join();
    if (result.generation != model_generation_) return;
    model_refreshing_ = false;
    test_pending_ = false;
    EnableWindow(model_refresh_, TRUE);
    InvalidateRect(model_refresh_, nullptr, FALSE);
    if (result.cancelled) return;
    if (!result.error.empty() || result.models.empty()) {
        test_success_ = false;
        auto message = result.error.empty()
            ? std::wstring(L"接口没有返回任何模型") : std::move(result.error);
        if (message.size() > 120) message.resize(120);
        set_status(message, true);
        return;
    }
    const auto keep = trim(text_of(model_));
    SendMessageW(model_, CB_RESETCONTENT, 0, 0);
    for (const auto& model : result.models) {
        SendMessageW(model_, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(model.c_str()));
    }
    const auto found = std::find(result.models.begin(), result.models.end(), keep);
    const bool kept = found != result.models.end();
    SendMessageW(model_, CB_SETCURSEL,
                 kept ? static_cast<WPARAM>(found - result.models.begin()) : 0, 0);
    clear_combo_edit_selection(model_);
    PostMessageW(window_, clear_selection_message, reinterpret_cast<WPARAM>(model_), 0);
    test_success_ = true;
    std::wstring message = L"拉到 " + std::to_wstring(result.models.size()) +
                           L" 个模型";
    if (!kept) message += L"，已选第一项";
    set_status(message, false);
}

void SettingsAiDialog::cancel_model_refresh() noexcept {
    ++model_generation_;
    if (model_thread_.joinable()) {
        model_thread_.request_stop();
        model_thread_.join();
    }
    discard_model_refresh_results(window_);
    model_refreshing_ = false;
    if (test_pending_) test_pending_ = false;
    if (model_refresh_ && IsWindow(model_refresh_)) {
        EnableWindow(model_refresh_, TRUE);
        InvalidateRect(model_refresh_, nullptr, FALSE);
    }
}

void SettingsAiDialog::discard_model_refresh_results(HWND target) noexcept {
    if (!target) return;
    MSG pending{};
    while (PeekMessageW(&pending, target, models_completed_message,
                        models_completed_message, PM_REMOVE)) {
        delete reinterpret_cast<ModelListResult*>(pending.lParam);
    }
}

void SettingsAiDialog::start_connection_test() {
    if (test_thread_.joinable()) return;
    settings_profiles::Profile profile;
    std::wstring error;
    if (!collect(profile, error)) {
        set_status(error, true);
        return;
    }
    const JsonObject options = settings_profiles::to_options(profile);
    EnableWindow(test_, FALSE);
    test_pending_ = true;
    test_success_ = false;
    set_status(L"测试中…", false);
    test_thread_ = connection_test::run(window_, test_completed_message, L"ai",
                                        options);
}

void SettingsAiDialog::finish_connection_test(bool success, std::wstring message) {
    if (test_thread_.joinable()) test_thread_.join();
    EnableWindow(test_, TRUE);
    test_pending_ = false;
    test_success_ = success;
    set_status(message, !success);
}

void SettingsAiDialog::discard_connection_test_results(HWND target) noexcept {
    connection_test::discard(target, test_completed_message);
}

void SettingsAiDialog::close(bool saved) {
    if (closed_) return;
    // The worker threads are stopped before the window goes away, so a late
    // completion can never post into a destroyed dialog.
    cancel_model_refresh();
    if (test_thread_.joinable()) {
        test_thread_.request_stop();
        test_thread_.join();
    }
    discard_connection_test_results(window_);
    settings_profiles::Profile profile;
    std::wstring error;
    result_.saved = saved && collect(profile, error);
    if (result_.saved) {
        profile.enabled = original_.enabled;
        result_.profile = std::move(profile);
    }
    closed_ = true;
    const HWND window = window_;
    if (window && IsWindow(window)) {
        ShowWindow(window, SW_HIDE);
        DestroyWindow(window);
    }
    window_ = nullptr;
}

void SettingsAiDialog::paint() {
    PAINTSTRUCT state{};
    HDC target = BeginPaint(window_, &state);
    if (!target) return;
    RECT client{};
    GetClientRect(window_, &client);
    if (client.right <= client.left || client.bottom <= client.top) {
        EndPaint(window_, &state);
        return;
    }
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = dc ? CreateCompatibleBitmap(target, client.right, client.bottom)
                        : nullptr;
    if (!dc || !bitmap) {
        FillRect(target, &client, background_);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        EndPaint(window_, &state);
        return;
    }
    const auto old_bitmap = SelectObject(dc, bitmap);
    FillRect(dc, &client, background_);
    fill_round_rect(dc, placement_.card, px(7, dpi_), color_card, color_line);
    // The 协议 caption keeps its label like every other field.
    for (std::size_t index = 0; index < field_labels.size(); ++index) {
        draw_text(dc, field_labels[index], placement_.labels[index], small_font_,
                  color_text_dim, DT_LEFT | DT_VCENTER | DT_SINGLELINE |
                                      DT_END_ELLIPSIS);
    }
    const HWND well_fields[3] = {name_, endpoint_, key_};
    const std::array<std::size_t, 3> well_shells{0, 2, 3};
    for (std::size_t index = 0; index < 3; ++index) {
        fill_round_rect(dc, placement_.shells[well_shells[index]],
                        px(logical_edit_corner_radius, dpi_), color_input,
                        color_line);
    }
    // 协议 chips paint themselves as three separate buttons; the band behind
    // them stays the card surface.
    if (!status_text_.empty()) {
        const COLORREF color = status_error_ ? color_bad
                               : test_success_ ? color_text : color_text_dim;
        draw_text(dc, status_text_, placement_.status, small_font_, color,
                  DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }
    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window_, &state);
}

void SettingsAiDialog::draw_combo(HWND control, HDC dc) {
    ui_controls::draw_combo_field(control, dc, text_of(control), font_, dpi_,
                                  &settings_internal::fill_round_rect);
}

LRESULT SettingsAiDialog::draw_item(const DRAWITEMSTRUCT& item) {
    HDC dc = item.hDC;
    RECT rect = item.rcItem;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hot = (item.itemState & ODS_HOTLIGHT) != 0;

    if (item.CtlType == ODT_COMBOBOX) {
        ui_controls::draw_combo_item(item, font_, dpi_, color_card,
                                     &settings_internal::fill_round_rect);
        return TRUE;
    }

    if (item.CtlType != ODT_BUTTON) return FALSE;
    // 协议 is three separate chips: the active one reads as a raised field on
    // the card surface (card fill + hairline border, the palette every other
    // control uses in both themes), the inactive ones sit in the input fill
    // and brighten on hover.
    for (std::size_t chip = 0; chip < protocol_segment_.size(); ++chip) {
        if (item.hwndItem == protocol_segment_[chip]) {
            const bool active =
                chip == static_cast<std::size_t>(protocol_segment());
            // The selected chip uses the same raised treatment as the
            // 传统翻译 / 大模型翻译 switch: nav-active fill with the accent
            // caption, no focus recolour anywhere.
            const COLORREF fill = active ? color_nav_active
                                  : hot != FALSE ? theme::color_hover
                                                 : color_input;
            // The chip sits on the card, so its corners must erase with the
            // card colour before the antialiased frame is drawn on top.
            FillRect(dc, &rect, card_background_);
            fill_round_rect(dc, rect, px(logical_edit_corner_radius, dpi_),
                            fill, fill);
            draw_text(dc, ai_protocol_segments[chip], rect, font_,
                      active ? theme::color_text
                             : hot != FALSE ? color_text : color_text_dim,
                      DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            return TRUE;
        }
    }
    // Every action button shares the quiet style and erases with the card
    // colour: these buttons sit on the card, so erasing with the page colour
    // would leave square corners visible around the antialiased frame.
    FillRect(dc, &rect, card_background_);
    const COLORREF fill = disabled ? color_line
                          : (pressed || hot) ? theme::color_hover : color_input;
    fill_round_rect(dc, rect, px(logical_edit_corner_radius, dpi_), fill,
                    color_line);
    draw_text(dc, text_of(item.hwndItem), rect, font_,
              disabled ? color_text_faint : color_text_dim,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    return TRUE;
}

LRESULT CALLBACK SettingsAiDialog::edit_proc(HWND control, UINT message,
                                             WPARAM wparam, LPARAM lparam,
                                             UINT_PTR subclass_id,
                                             DWORD_PTR reference) {
    auto* self = reinterpret_cast<SettingsAiDialog*>(reference);
    const auto update_region = [&] {
        if (!self) return;
        RECT rect{};
        GetClientRect(control, &rect);
        if (rect.right <= rect.left || rect.bottom <= rect.top) return;
        const int inset = std::max(1, px(logical_edit_inset_x, self->dpi_));
        const int radius = std::max(
            1, px(logical_edit_corner_radius, self->dpi_) - inset);
        set_rounded_control_region(control, radius);
    };
    switch (message) {
    case WM_NCPAINT:
        // The dialog owns the edit frame; a native non-client paint would put a
        // square system border over the rounded well.
        return 0;
    case WM_SIZE: {
        const auto result = DefSubclassProc(control, message, wparam, lparam);
        update_region();
        return result;
    }
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        const auto result = DefSubclassProc(control, message, wparam, lparam);
        if (self && self->window_) {
            InvalidateRect(self->window_, nullptr, FALSE);
        }
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, &SettingsAiDialog::edit_proc, subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT CALLBACK SettingsAiDialog::combo_proc(HWND control, UINT message,
                                              WPARAM wparam, LPARAM lparam,
                                              UINT_PTR subclass_id,
                                              DWORD_PTR reference) {
    auto* self = reinterpret_cast<SettingsAiDialog*>(reference);
    switch (message) {
    case WM_SIZE: {
        const auto result = DefSubclassProc(control, message, wparam, lparam);
        if (self) align_combo_edit(control, self->dpi_);
        return result;
    }
    case WM_PAINT: {
        PAINTSTRUCT state{};
        HDC dc = BeginPaint(control, &state);
        if (dc && self) {
            try {
                self->draw_combo(control, dc);
            } catch (...) {
                RECT rect{};
                GetClientRect(control, &rect);
                FillRect(dc, &rect, self->input_background_);
            }
        }
        EndPaint(control, &state);
        return 0;
    }
    case WM_PRINTCLIENT:
        if (self) {
            try {
                self->draw_combo(control, reinterpret_cast<HDC>(wparam));
            } catch (...) {
                return 0;
            }
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        const auto result = DefSubclassProc(control, message, wparam, lparam);
        if (message == WM_SETFOCUS && self) align_combo_edit(control, self->dpi_);
        clear_combo_edit_selection(control);
        InvalidateRect(control, nullptr, FALSE);
        return result;
    }
    case WM_LBUTTONDOWN: {
        set_combo_edit_redraw(control, false);
        const auto result = DefSubclassProc(control, message, wparam, lparam);
        if (self) align_combo_edit(control, self->dpi_);
        clear_combo_edit_selection(control);
        // An owner-drawn drop-down combo swallows the click that should open
        // its list, so the drop-down is toggled here: pressing the field
        // always shows the model list, pressing again closes it.
        if (!SendMessageW(control, CB_GETDROPPEDSTATE, 0, 0)) {
            SendMessageW(control, CB_SHOWDROPDOWN, TRUE, 0);
            set_combo_edit_redraw(control, true);
        }
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, &SettingsAiDialog::combo_proc, subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT CALLBACK SettingsAiDialog::combo_edit_proc(HWND control, UINT message,
                                                   WPARAM wparam, LPARAM lparam,
                                                   UINT_PTR subclass_id,
                                                   DWORD_PTR reference) {
    auto* self = reinterpret_cast<SettingsAiDialog*>(reference);
    switch (message) {
    case WM_SETFOCUS:
    case WM_LBUTTONDOWN:
    case WM_WINDOWPOSCHANGED: {
        const auto result = DefSubclassProc(control, message, wparam, lparam);
        const HWND combo = GetParent(control);
        if (self && combo) align_combo_edit(combo, self->dpi_);
        if (message == WM_SETFOCUS) clear_combo_edit_selection(combo);
        // Clicking the embedded edit is the natural way to open the list: the
        // owner-drawn combo no longer receives the click itself, so the
        // drop-down is opened from here when it is still closed.
        if (message == WM_LBUTTONDOWN && combo &&
            !SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0)) {
            SendMessageW(combo, CB_SHOWDROPDOWN, TRUE, 0);
        }
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, &SettingsAiDialog::combo_edit_proc,
                             subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT SettingsAiDialog::handle_dialog_message(UINT message, WPARAM wparam,
                                                LPARAM lparam) {
    switch (message) {
    case WM_SIZE:
        layout_controls();
        return 0;
    case WM_GETMINMAXINFO: {
        auto* limits = reinterpret_cast<MINMAXINFO*>(lparam);
        RECT desired{0, 0, px(logical_width, dpi_), px(logical_height, dpi_)};
        AdjustWindowRectExForDpi(&desired,
                                 static_cast<DWORD>(GetWindowLongPtrW(window_,
                                                                      GWL_STYLE)),
                                 FALSE, WS_EX_TOOLWINDOW | WS_EX_COMPOSITED,
                                 static_cast<UINT>(dpi_));
        // The dialog keeps a fixed size: the field stack has no meaningful
        // resize behaviour and a stretched card would look broken.
        limits->ptMinTrackSize.x = desired.right - desired.left;
        limits->ptMinTrackSize.y = desired.bottom - desired.top;
        limits->ptMaxTrackSize.x = desired.right - desired.left;
        limits->ptMaxTrackSize.y = desired.bottom - desired.top;
        return 0;
    }
    case WM_DPICHANGED: {
        dpi_ = HIWORD(wparam);
        const auto* suggested = reinterpret_cast<RECT*>(lparam);
        SetWindowPos(window_, nullptr, suggested->left, suggested->top,
                     suggested->right - suggested->left,
                     suggested->bottom - suggested->top,
                     SWP_NOACTIVATE | SWP_NOZORDER);
        create_theme_resources();
        for (HWND control : {name_, endpoint_, key_, model_,
                             model_refresh_, test_, save_,
                             protocol_segment_[0], protocol_segment_[1],
                             protocol_segment_[2]}) {
            set_font(control, font_);
        }
        layout_controls();
        RedrawWindow(window_, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_ERASE | RDW_UPDATENOW);
        return 0;
    }
    case WM_MEASUREITEM: {
        auto* measure = reinterpret_cast<MEASUREITEMSTRUCT*>(lparam);
        if (measure->CtlType == ODT_COMBOBOX) {
            measure->itemHeight = static_cast<UINT>(px(30, dpi_));
            return TRUE;
        }
        break;
    }
    case WM_DRAWITEM:
        return draw_item(*reinterpret_cast<DRAWITEMSTRUCT*>(lparam));
    case WM_PAINT:
        paint();
        return 0;
    case models_completed_message: {
        std::unique_ptr<ModelListResult> result(
            reinterpret_cast<ModelListResult*>(lparam));
        if (result) finish_model_refresh(std::move(*result));
        return 0;
    }
    case test_completed_message: {
        std::unique_ptr<std::pair<bool, std::wstring>> result(
            reinterpret_cast<std::pair<bool, std::wstring>*>(lparam));
        if (result) finish_connection_test(result->first, std::move(result->second));
        return 0;
    }
    case clear_selection_message: {
        const HWND combo = reinterpret_cast<HWND>(wparam);
        if (combo && IsWindow(combo)) {
            clear_combo_edit_selection(combo);
            align_combo_edit(combo, dpi_);
            RedrawWindow(window_, nullptr, nullptr,
                         RDW_INVALIDATE | RDW_ALLCHILDREN | RDW_UPDATENOW);
        }
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notification = HIWORD(wparam);
        if (notification == CBN_DROPDOWN &&
            reinterpret_cast<HWND>(lparam) == model_) {
            // The drop-down list is sized and placed only now: before the
            // first show its rect is empty, and styling it in layout_controls
            // parked the list off-screen, which is why the model list would
            // not drop down.
            style_combo_popup(model_, dpi_, font_);
            InvalidateRect(model_, nullptr, FALSE);
            return 0;
        }
        // The segmented 协议 row picks the wire format on click.
        if (notification == BN_CLICKED &&
            (id == id_protocol_chat || id == id_protocol_responses ||
             id == id_protocol_anthropic)) {
            if (!loading_) {
                set_protocol_segment(id == id_protocol_chat ? 0
                                     : id == id_protocol_responses ? 1 : 2);
            }
            return 0;
        }
        if (id == IDCANCEL) {
            close(false);
            return 0;
        }
        if (id == id_save && notification == BN_CLICKED) {
            settings_profiles::Profile profile;
            std::wstring error;
            if (!collect(profile, error)) {
                set_status(error, true);
                return 0;
            }
            close(true);
            return 0;
        }
        if (id == id_refresh_models && notification == BN_CLICKED) {
            start_model_refresh();
            return 0;
        }
        if (id == dlg_id_test && notification == BN_CLICKED) {
            test_success_ = false;
            start_connection_test();
            return 0;
        }
        break;
    }
    default:
        break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

}  // namespace minitrans
