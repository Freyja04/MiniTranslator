#include "provider_key_dialog.hpp"

#include "connection_test.hpp"
#include "settings_window_internal.hpp"
#include "theme.hpp"
#include "ui_draw.hpp"
#include "util.hpp"

#include <windowsx.h>
#include <uxtheme.h>
#include <winrt/Windows.Data.Json.h>

#include <array>
#include <memory>
#include <utility>

namespace minitrans {

using namespace settings_internal;
using ui_draw::draw_text;
using ui_draw::set_font;
using ui_draw::text_of;

namespace {

constexpr wchar_t key_dialog_class[] = L"MiniTranslator.Native.ProviderKeyDialog.v1";
constexpr int id_field = 7201;
constexpr int id_save = 7202;
constexpr int key_dialog_id_test = 7203;
constexpr int logical_width = 360;
constexpr int logical_height = 230;
constexpr int logical_margin = 18;
constexpr int key_dialog_card_padding = 14;
constexpr int logical_title_height = 26;
constexpr int logical_label_height = 18;
constexpr int logical_field_height = 34;
constexpr int logical_button_height = 32;
constexpr int logical_button_width = 88;
constexpr int key_dialog_status_height = 18;

}  // namespace

ProviderKeyDialog::Result ProviderKeyDialog::edit(HINSTANCE instance, HWND owner,
                                                  std::wstring_view title,
                                                  std::wstring_view current_key) {
    ProviderKeyDialog dialog(instance, owner, title, current_key);
    dialog.create_popup(key_dialog_class, L"接口密钥", logical_width,
                        logical_height);
    if (!dialog.window_) throw AppError("无法打开密钥编辑窗口");
    dialog.create_controls();
    dialog.run_modal();
    return dialog.result_;
}

void ProviderKeyDialog::self_test(HINSTANCE instance, HWND owner) {
    ProviderKeyDialog dialog(instance, owner, L"DeepL", L"");
    dialog.create_popup(key_dialog_class, L"接口密钥", logical_width,
                        logical_height);
    if (!dialog.window_) throw AppError("key dialog self-test window failed");
    dialog.create_controls();
    if (!dialog.field_ || !dialog.test_ || !dialog.save_ || !dialog.cancel_) {
        throw AppError("key dialog self-test did not create required controls");
    }
    if (dialog.dpi_ != static_cast<int>(GetDpiForWindow(dialog.window_))) {
        throw AppError("key dialog self-test DPI mismatch");
    }
    // The card ends after the status line, and the button row stays inside it.
    if (dialog.card_.bottom <= dialog.status_rect_.bottom ||
        dialog.save_rect_.right > dialog.card_.right ||
        dialog.test_rect_.right > dialog.save_rect_.left ||
        dialog.save_rect_.top < dialog.field_rect_.bottom) {
        throw AppError("key dialog self-test layout is misplaced");
    }
    // The edit carries the rounded region layout() applies, so a click on its
    // corner still reaches the field instead of the parent.
    {
        HRGN region = CreateRectRgn(0, 0, 0, 0);
        const int type = region ? GetWindowRgn(dialog.field_, region) : ERROR;
        if (region) DeleteObject(region);
        if (type == ERROR) {
            throw AppError("key dialog self-test field has no rounded region");
        }
    }
    DestroyWindow(dialog.window_);
    dialog.window_ = nullptr;
}

ProviderKeyDialog::ProviderKeyDialog(HINSTANCE instance, HWND owner,
                                     std::wstring_view title,
                                     std::wstring_view current_key)
    : ModalDialog(instance, owner), title_(title), key_(current_key) {}

ProviderKeyDialog::~ProviderKeyDialog() {
    if (test_thread_.joinable()) {
        test_thread_.request_stop();
        test_thread_.join();
    }
}

void ProviderKeyDialog::create_controls() {
    create_theme_resources();
    field_ = CreateWindowExW(0, L"EDIT", key_.c_str(),
                             WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL |
                                 ES_PASSWORD | WS_TABSTOP,
                             0, 0, 1, 1, window_,
                             reinterpret_cast<HMENU>(static_cast<INT_PTR>(id_field)),
                             instance_, nullptr);
    if (!field_) throw_last_error("create key field");
    set_font(field_, font_);
    SetWindowTheme(field_, L"", L"");
    SetWindowLongPtrW(field_, GWL_STYLE,
                      GetWindowLongPtrW(field_, GWL_STYLE) &
                          ~static_cast<LONG_PTR>(WS_BORDER));
    const auto extended = GetWindowLongPtrW(field_, GWL_EXSTYLE);
    SetWindowLongPtrW(field_, GWL_EXSTYLE,
                      extended & ~(static_cast<LONG_PTR>(WS_EX_CLIENTEDGE)));
    SetWindowPos(field_, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_FRAMECHANGED);
    SendMessageW(field_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(px(9, dpi_), px(9, dpi_)));
    // The edit's rounded region is applied in layout(), once the field has
    // its final size: a region created while the control is still 1x1 leaves
    // only one clickable pixel, and every later click lands on the parent.
    const auto make_button = [&](const wchar_t* text, int identifier) {
        HWND control = CreateWindowExW(
            0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | BS_OWNERDRAW | BS_NOTIFY | WS_TABSTOP,
            0, 0, 1, 1, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)),
            instance_, nullptr);
        if (!control) throw_last_error("create key dialog button");
        set_font(control, font_);
        return control;
    };
    test_ = make_button(L"测试连接", key_dialog_id_test);
    save_ = make_button(L"保存", id_save);
    cancel_ = make_button(L"取消", IDCANCEL);
    layout();
}

void ProviderKeyDialog::layout() {
    RECT client{};
    GetClientRect(window_, &client);
    const int margin = px(logical_margin, dpi_);
    const int padding = px(key_dialog_card_padding, dpi_);
    card_ = {margin, margin, client.right - margin, client.bottom - margin};
    // The card title carries the provider name, then the API Key caption and
    // the field, and one 保存 button closing the row bottom-right - the same
    // rhythm as the endpoint dialog.
    int y = card_.top + padding;
    title_rect_ = {card_.left + padding, y, card_.right - padding,
                   y + px(logical_title_height, dpi_)};
    y = title_rect_.bottom + px(10, dpi_);
    label_rect_ = {card_.left + padding, y, card_.right - padding,
                   y + px(logical_label_height, dpi_)};
    y = label_rect_.bottom + px(4, dpi_);
    field_rect_ = {card_.left + padding, y, card_.right - padding,
                   y + px(logical_field_height, dpi_)};
    y = field_rect_.bottom + px(12, dpi_);
    // 测试连接 sits left of 保存, and the outcome line runs under the row.
    const int button_width = px(logical_button_width, dpi_);
    const int gap = px(6, dpi_);
    save_rect_ = {card_.right - padding - button_width, y,
                  card_.right - padding, y + px(logical_button_height, dpi_)};
    test_rect_ = {save_rect_.left - gap - button_width, save_rect_.top,
                  save_rect_.left - gap, save_rect_.bottom};
    y = save_rect_.bottom + px(8, dpi_);
    status_rect_ = {card_.left + padding, y, card_.right - padding,
                    y + px(key_dialog_status_height, dpi_)};
    card_.bottom = status_rect_.bottom + padding;
    // The edit is inset inside the well exactly like the endpoint dialog's
    // fields, and gets the same rounded region.
    const int inset_x = std::max(1, px(logical_edit_inset_x, dpi_));
    const int inset_top = std::max(1, px(logical_edit_inset_top, dpi_));
    const int inset_bottom = std::max(1, px(logical_edit_inset_bottom, dpi_));
    MoveWindow(field_, field_rect_.left + inset_x, field_rect_.top + inset_top,
               std::max(1, static_cast<int>(field_rect_.right -
                                            field_rect_.left) - inset_x * 2),
               std::max(1, static_cast<int>(field_rect_.bottom -
                                            field_rect_.top) -
                              inset_top - inset_bottom),
               TRUE);
    // Now that the edit has its real size, give it the rounded region - the
    // same treatment the endpoint dialog's edits get from their subclass.
    {
        RECT clip{};
        GetClientRect(field_, &clip);
        if (clip.right > clip.left && clip.bottom > clip.top) {
            const int radius =
                std::max(1, px(logical_edit_corner_radius, dpi_) -
                                px(logical_edit_inset_x, dpi_));
            const int diameter = radius * 2;
            HRGN region = CreateRoundRectRgn(0, 0, clip.right + 1,
                                             clip.bottom + 1, diameter, diameter);
            if (region && !SetWindowRgn(field_, region, TRUE)) {
                DeleteObject(region);
            }
        }
    }
    MoveWindow(save_, save_rect_.left, save_rect_.top,
               save_rect_.right - save_rect_.left,
               save_rect_.bottom - save_rect_.top, TRUE);
    MoveWindow(test_, test_rect_.left, test_rect_.top,
               test_rect_.right - test_rect_.left,
               test_rect_.bottom - test_rect_.top, TRUE);
    // 取消 has no button: Esc or the close path abandons the edit, the same
    // contract as the endpoint dialog.
    ShowWindow(cancel_, SW_HIDE);
    InvalidateRect(window_, nullptr, TRUE);
}

LRESULT ProviderKeyDialog::handle_dialog_message(UINT message, WPARAM wparam,
                                                 LPARAM lparam) {
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT state{};
        HDC target = BeginPaint(window_, &state);
        if (target) {
            RECT client{};
            GetClientRect(window_, &client);
            FillRect(target, &client, background_);
            fill_round_rect(target, card_, px(7, dpi_), color_card, color_line);
            draw_text(target, title_, title_rect_, font_, color_text,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            draw_text(target, L"API Key", label_rect_, font_, color_text_dim,
                      DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            // The edit sits in a rounded well this window paints, matching the
            // endpoint dialog's field frames.
            fill_round_rect(target, field_rect_,
                            px(logical_edit_corner_radius, dpi_), color_input,
                            color_line);
            if (!status_text_.empty()) {
                draw_text(target, status_text_, status_rect_, font_,
                          status_error_ ? color_bad : color_text_dim,
                          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
            }
            EndPaint(window_, &state);
        }
        return 0;
    }
    case WM_DRAWITEM: {
        const auto* item = reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
        if (!item || item->CtlType != ODT_BUTTON) return FALSE;
        const RECT rect = item->rcItem;
        const bool hot = (item->itemState & ODS_HOTLIGHT) != 0;
        const bool pressed = (item->itemState & ODS_SELECTED) != 0;
        // Same quiet button as the endpoint dialog's row: no border, hover
        // brightens the fill.
        const COLORREF fill =
            (pressed || hot) ? theme::color_hover : color_input;
        FillRect(item->hDC, &rect, card_background_);
        fill_round_rect(item->hDC, rect, px(6, dpi_), fill, fill);
        draw_text(item->hDC, text_of(item->hwndItem), rect, font_,
                  color_text_dim,
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return TRUE;
    }
    case test_completed_message: {
        std::unique_ptr<std::pair<bool, std::wstring>> result(
            reinterpret_cast<std::pair<bool, std::wstring>*>(lparam));
        if (result) finish_connection_test(result->first, std::move(result->second));
        return 0;
    }
    case WM_COMMAND: {
        const int id = LOWORD(wparam);
        const int notification = HIWORD(wparam);
        if (id == key_dialog_id_test && notification == BN_CLICKED) {
            start_connection_test();
            return 0;
        }
        if (id == id_save && notification == BN_CLICKED) {
            close(true);
            return 0;
        }
        break;
    }
    default:
        break;
    }
    return DefWindowProcW(window_, message, wparam, lparam);
}

void ProviderKeyDialog::start_connection_test() {
    if (test_thread_.joinable()) return;
    const std::wstring key = trim(text_of(field_));
    if (key.empty()) {
        status_error_ = true;
        status_text_ = L"先填写 API Key 再测试";
        InvalidateRect(window_, nullptr, TRUE);
        return;
    }
    test_pending_ = true;
    status_error_ = false;
    status_text_ = L"测试中…";
    EnableWindow(test_, FALSE);
    InvalidateRect(window_, nullptr, TRUE);
    winrt::Windows::Data::Json::JsonObject options;
    options.SetNamedValue(
        L"key", winrt::Windows::Data::Json::JsonValue::CreateStringValue(key));
    options.SetNamedValue(
        L"free_plan", winrt::Windows::Data::Json::JsonValue::CreateBooleanValue(true));
    test_thread_ = connection_test::run(window_, test_completed_message, L"deepl",
                                        std::move(options));
}

void ProviderKeyDialog::finish_connection_test(bool success,
                                               std::wstring message) {
    if (test_thread_.joinable()) test_thread_.join();
    test_pending_ = false;
    status_error_ = !success;
    status_text_ = std::move(message);
    if (test_ && IsWindow(test_)) EnableWindow(test_, TRUE);
    InvalidateRect(window_, nullptr, TRUE);
}

void ProviderKeyDialog::close(bool saved) {
    if (closed_) return;
    result_.saved = saved;
    if (saved) result_.key = text_of(field_);
    closed_ = true;
    const HWND window = window_;
    if (window && IsWindow(window)) DestroyWindow(window);
    window_ = nullptr;
}

}  // namespace minitrans
