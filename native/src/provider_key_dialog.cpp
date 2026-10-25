#include "provider_key_dialog.hpp"

#include "http.hpp"
#include "settings_window_internal.hpp"
#include "theme.hpp"
#include "translator.hpp"
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
    dialog.create_window();
    if (!dialog.window_) throw AppError("无法打开密钥编辑窗口");
    dialog.create_controls();
    ShowWindow(dialog.window_, SW_SHOW);
    SetForegroundWindow(dialog.window_);
    SetFocus(dialog.field_);

    MSG message{};
    while (!dialog.closed_ && GetMessageW(&message, nullptr, 0, 0) > 0) {
        if (message.message == WM_QUIT) {
            dialog.close(false);
            break;
        }
        // Esc closes this dialog and nothing else: swallow both the keydown
        // and the WM_CHAR it produces so no leftover keystroke reaches the
        // settings window after the dialog closes.
        if ((message.message == WM_KEYDOWN &&
             static_cast<UINT>(message.wParam) == VK_ESCAPE) ||
            (message.message == WM_CHAR &&
             static_cast<wchar_t>(message.wParam) == 0x1B)) {
            dialog.close(false);
            continue;
        }
        const HWND window = dialog.window_;
        if (window && IsWindow(window) && IsWindowVisible(window) &&
            IsDialogMessageW(window, &message)) {
            continue;
        }
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    return dialog.result_;
}

ProviderKeyDialog::ProviderKeyDialog(HINSTANCE instance, HWND owner,
                                     std::wstring_view title,
                                     std::wstring_view current_key)
    : instance_(instance), owner_(owner), title_(title), key_(current_key) {}

ProviderKeyDialog::~ProviderKeyDialog() {
    if (test_thread_.joinable()) {
        test_thread_.request_stop();
        test_thread_.join();
    }
    if (window_ && IsWindow(window_)) DestroyWindow(window_);
    if (font_) DeleteObject(font_);
    if (background_) DeleteObject(background_);
    if (card_background_) DeleteObject(card_background_);
    if (input_background_) DeleteObject(input_background_);
}

void ProviderKeyDialog::create_window() {
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = &ProviderKeyDialog::window_proc;
    description.hInstance = instance_;
    description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    description.lpszClassName = key_dialog_class;
    if (!RegisterClassExW(&description) &&
        GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw_last_error("register key dialog");
    }
    dpi_ = owner_ && IsWindow(owner_)
        ? static_cast<int>(GetDpiForWindow(owner_)) : 96;
    // Borderless popup like the endpoint dialog: the card carries the title,
    // the system caption has nothing to add.
    const DWORD style = WS_POPUP;
    constexpr DWORD popup_ex_style = WS_EX_TOOLWINDOW | WS_EX_COMPOSITED;
    RECT desired{0, 0, px(logical_width, dpi_), px(logical_height, dpi_)};
    AdjustWindowRectExForDpi(&desired, style, FALSE, popup_ex_style,
                             static_cast<UINT>(dpi_));
    RECT anchor{};
    if (owner_ && IsWindow(owner_) && GetWindowRect(owner_, &anchor)) {
        // Nothing to do: anchor holds the owner rect.
    }
    const int width = desired.right - desired.left;
    const int height = desired.bottom - desired.top;
    const int x = anchor.left +
        std::max(0L, (anchor.right - anchor.left - width) / 2);
    const int y = anchor.top +
        std::max(0L, (anchor.bottom - anchor.top - height) / 2);
    window_ = CreateWindowExW(popup_ex_style, key_dialog_class, L"接口密钥", style,
                              x, y, width, height, owner_, nullptr, instance_,
                              this);
    if (!window_) {
        if (!window_error_.empty()) throw AppError(window_error_);
        throw_last_error("create key dialog");
    }
    theme::apply_window_theme(window_, true, true);
}

void ProviderKeyDialog::create_controls() {
    font_ = CreateFontW(-px(theme::font_size_body, dpi_), 0, 0, 0, FW_NORMAL,
                        FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
                        CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                        theme::font_family.c_str());
    background_ = CreateSolidBrush(color_page);
    card_background_ = CreateSolidBrush(color_card);
    input_background_ = CreateSolidBrush(color_input);
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

LRESULT ProviderKeyDialog::handle_message(UINT message, WPARAM wparam,
                                          LPARAM lparam) {
    switch (message) {
    case WM_CREATE:
        return 0;
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
    case WM_ERASEBKGND:
        return 1;
    case WM_CTLCOLOREDIT: {
        const auto dc = reinterpret_cast<HDC>(wparam);
        SetTextColor(dc, color_text);
        SetBkColor(dc, color_input);
        SetBkMode(dc, OPAQUE);
        return reinterpret_cast<LRESULT>(input_background_);
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
        if (id == IDCANCEL) {
            close(false);
            return 0;
        }
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
    case WM_CLOSE:
        close(false);
        return 0;
    case WM_NCDESTROY: {
        const HWND destroyed = window_;
        closed_ = true;
        SetWindowLongPtrW(destroyed, GWLP_USERDATA, 0);
        window_ = nullptr;
        return DefWindowProcW(destroyed, message, wparam, lparam);
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
    const HWND target = window_;
    const std::wstring title = title_;
    test_thread_ = std::jthread(
        [target, key, title](std::stop_token stop) noexcept {
            auto* raw = new (std::nothrow) std::pair<bool, std::wstring>();
            if (!raw) return;
            try {
                winrt::Windows::Data::Json::JsonObject options;
                options.SetNamedValue(
                    L"key",
                    winrt::Windows::Data::Json::JsonValue::CreateStringValue(key));
                options.SetNamedValue(
                    L"free_plan",
                    winrt::Windows::Data::Json::JsonValue::CreateBooleanValue(true));
                WinrtApartment apartment(winrt::apartment_type::multi_threaded);
                TranslatorService service;
                const auto translated = service.translate(
                    L"deepl", options, {L"Hello"}, L"zh-Hans", std::nullopt, stop);
                raw->first = translated.size() == 1 && !translated.front().empty();
                raw->second = raw->first ? L"连接正常" : L"接口没有返回译文";
            } catch (const std::exception& error) {
                raw->first = false;
                raw->second = redact_sensitive(utf8_to_wide(error.what()));
            } catch (...) {
                raw->first = false;
                raw->second = L"连接测试遇到未知错误";
            }
            if (!PostMessageW(target, ProviderKeyDialog::test_completed_message,
                              raw->first ? 1 : 0,
                              reinterpret_cast<LPARAM>(raw))) {
                delete raw;
            }
        });
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

LRESULT CALLBACK ProviderKeyDialog::window_proc(HWND window, UINT message,
                                                WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<ProviderKeyDialog*>(
        GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<ProviderKeyDialog*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wparam, lparam);
    try {
        return self->handle_message(message, wparam, lparam);
    } catch (const std::exception& error) {
        self->window_error_ = error.what();
    } catch (...) {
        self->window_error_ = "unexpected key dialog error";
    }
    if (message == WM_NCCREATE) return FALSE;
    if (message == WM_CREATE) return -1;
    self->close(false);
    return 0;
}

}  // namespace minitrans
