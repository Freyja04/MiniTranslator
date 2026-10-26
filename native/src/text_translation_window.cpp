#include "text_translation_window_internal.hpp"

#include "language.hpp"
#include "resource.h"
#include "self_test_support.hpp"
#include "theme.hpp"
#include "ui_controls.hpp"
#include "ui_icons.hpp"
#include "util.hpp"

#include <commctrl.h>
#include <dwmapi.h>
#include <psapi.h>
#include <tlhelp32.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <algorithm>
#include <array>

namespace minitrans {


QuickTranslateWindow::QuickTranslateWindow(HINSTANCE instance,
                                           TextTranslationSession& session)
    : instance_(instance), session_(session) {}

QuickTranslateWindow::~QuickTranslateWindow() {
    if (window_) DestroyWindow(window_);
    destroy_theme_resources();
}

bool QuickTranslateWindow::visible() const noexcept {
    return window_ && IsWindowVisible(window_);
}

void QuickTranslateWindow::create_window(HWND owner) {
    if (window_) return;
    register_window_class(instance_, quick_window_class, &QuickTranslateWindow::window_proc);
    window_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW, quick_window_class, MINITRANS_DISPLAY_NAME,
        WS_POPUP | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
        quick_window_logical_width, quick_window_logical_height,
        owner, nullptr, instance_, this);
    if (!window_) throw_last_error("create quick translation window");
    dpi_ = static_cast<int>(GetDpiForWindow(window_));
    apply_dark_frame(window_, true);
    create_theme_resources();
    create_controls();
    refresh();
}

void QuickTranslateWindow::create_theme_resources() {
    destroy_theme_resources();
    // The quick window keeps no green: focus and hover borders read through
    // the same neutral hairline as the rest of the chrome.
    accent_ = theme::color_line_high;
    font_ = create_font(dpi_, font_size_translation, FW_NORMAL,
                        theme::font_family);
    small_font_ = create_font(dpi_, font_size_small, FW_NORMAL,
                              theme::font_family);
    icon_font_ = create_icon_font(dpi_, 15);
    action_icon_font_ = create_icon_font(dpi_, 12);
    // The close cross reads heavier than the capture glyph at the same size.
    close_icon_font_ = create_icon_font(dpi_, 13);
    background_ = CreateSolidBrush(color_background);
    surface_ = CreateSolidBrush(color_surface);
    input_background_ = CreateSolidBrush(color_surface);
}

void QuickTranslateWindow::destroy_theme_resources() noexcept {
    if (font_) DeleteObject(font_);
    if (small_font_) DeleteObject(small_font_);
    if (icon_font_) DeleteObject(icon_font_);
    if (action_icon_font_) DeleteObject(action_icon_font_);
    if (close_icon_font_) DeleteObject(close_icon_font_);
    if (background_) DeleteObject(background_);
    if (surface_) DeleteObject(surface_);
    if (input_background_) DeleteObject(input_background_);
    font_ = small_font_ = icon_font_ = action_icon_font_ = nullptr;
    close_icon_font_ = nullptr;
    background_ = surface_ = input_background_ = nullptr;
}


void QuickTranslateWindow::create_controls() {
    source_combo_ = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
        0, 0, 0, 0, window_, control_id(id_source), instance_, nullptr);
    swap_button_ = CreateWindowExW(0, L"BUTTON", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, window_, control_id(id_swap), instance_, nullptr);
    target_combo_ = CreateWindowExW(0, L"COMBOBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL |
        CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS,
        0, 0, 0, 0, window_, control_id(id_target), instance_, nullptr);
    input_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
        0, 0, 0, 0, window_, control_id(id_input), instance_, nullptr);
    output_ = CreateWindowExW(0, L"EDIT", L"",
        WS_CHILD | WS_VISIBLE |
        ES_MULTILINE | ES_AUTOVSCROLL | ES_READONLY,
        0, 0, 0, 0, window_, control_id(id_output), instance_, nullptr);
    capture_button_ = CreateWindowExW(0, L"BUTTON", L"截图翻译",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, window_, control_id(id_quick_capture), instance_, nullptr);
    home_button_ = CreateWindowExW(0, L"BUTTON", L"首页",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, window_, control_id(id_quick_home), instance_, nullptr);
    close_button_ = CreateWindowExW(0, L"BUTTON", L"关闭",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, window_, control_id(id_quick_close), instance_, nullptr);
    copy_button_ = CreateWindowExW(0, L"BUTTON", L"复制",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, window_, control_id(id_copy), instance_, nullptr);
    clear_button_ = CreateWindowExW(0, L"BUTTON", L"清空",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
        0, 0, 0, 0, window_, control_id(id_clear), instance_, nullptr);
    for (HWND control : {source_combo_, swap_button_, target_combo_, input_, output_,
                         capture_button_, home_button_, close_button_, copy_button_,
                         clear_button_}) {
        theme::apply_control_theme(control);
        set_control_font(control, font_);
    }
    // No left margin: the text starts at the same x as the 原文/译文 captions.
    SendMessageW(input_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(0, scaled(12, dpi_)));
    SendMessageW(output_, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                 MAKELPARAM(0, scaled(12, dpi_)));
    SendMessageW(input_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"输入或粘贴要翻译的文字"));
    SendMessageW(output_, EM_SETCUEBANNER, TRUE,
                 reinterpret_cast<LPARAM>(L"译文"));
    SetWindowSubclass(output_, &keep_editor_unfocused, output_editor_subclass_id,
                      reinterpret_cast<DWORD_PTR>(input_));
    populate_targets(source_combo_, false);
    populate_targets(target_combo_, false);
    SetWindowSubclass(source_combo_, &QuickTranslateWindow::target_combo_proc,
                      source_combo_subclass_id,
                      reinterpret_cast<DWORD_PTR>(this));
    SetWindowSubclass(target_combo_, &QuickTranslateWindow::target_combo_proc,
                      target_combo_subclass_id,
                      reinterpret_cast<DWORD_PTR>(this));
    style_target_popup(source_combo_, dpi_);
    style_target_popup(target_combo_, dpi_);
    SetWindowSubclass(input_, &QuickTranslateWindow::input_proc, input_subclass_id,
                      reinterpret_cast<DWORD_PTR>(this));
    add_tooltip(window_, copy_button_, L"复制译文");
    add_tooltip(window_, clear_button_, L"清空原文");
    add_tooltip(window_, swap_button_, L"对调原文与译文语种");
    add_tooltip(window_, capture_button_, L"截图翻译");
    add_tooltip(window_, home_button_, L"首页");
    add_tooltip(window_, close_button_, L"关闭");
    layout_controls();
}
void QuickTranslateWindow::layout_controls() {
    if (!window_) return;
    RECT client{};
    GetClientRect(window_, &client);
    const int width = client.right;
    const int height = client.bottom;
    const int margin = scaled(16, dpi_);
    const int gap = scaled(8, dpi_);
    const int header_top = scaled(10, dpi_);
    const int control_height = scaled(30, dpi_);
    const int icon_width = scaled(30, dpi_);
    const int combo_width = scaled(96, dpi_);
    // A drop-down combo uses its window height for the open list as well, so
    // size it from the item count: enough rows to avoid a scrollbar, but not
    // taller than the twelve-row cap shared with the settings window.
    const auto combo_height_for = [&](HWND combo) {
        const int count = std::clamp(
            static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0)), 1, 12);
        return control_height + count * scaled(30, dpi_);
    };

    // The language selectors sit on the left edge; capture, home and close
    // share the right edge in that order.
    MoveWindow(source_combo_, margin, header_top, combo_width,
               combo_height_for(source_combo_), TRUE);
    MoveWindow(swap_button_, margin + combo_width + gap, header_top, icon_width,
               control_height, TRUE);
    MoveWindow(target_combo_, margin + combo_width + gap * 2 + icon_width,
               header_top, combo_width, combo_height_for(target_combo_), TRUE);
    MoveWindow(close_button_, width - margin - icon_width, header_top,
               icon_width, control_height, TRUE);
    MoveWindow(home_button_, width - margin - icon_width * 2 - gap, header_top,
               icon_width, control_height, TRUE);
    MoveWindow(capture_button_, width - margin - icon_width * 3 - gap * 2,
               header_top, icon_width, control_height, TRUE);

    const int body_top = header_top + control_height + scaled(14, dpi_);
    const int body_bottom = height - scaled(34, dpi_);
    const int inset = scaled(13, dpi_);
    const int label_height = scaled(29, dpi_);
    const int action_width = scaled(26, dpi_);
    const int action_height = scaled(22, dpi_);
    const int divider_y = body_top + (body_bottom - body_top) / 2;
    const int content_right = width - margin - inset;
    const int edit_left = margin + inset;
    const int edit_width = std::max(1, content_right - edit_left);

    MoveWindow(input_, edit_left, body_top + label_height, edit_width,
               std::max(1, divider_y - body_top - label_height), TRUE);
    MoveWindow(output_, edit_left, divider_y + label_height, edit_width,
               std::max(1, body_bottom - divider_y - label_height - 1), TRUE);

    // The clear and copy buttons follow their 原文/译文 captions instead of the
    // card's right edge, so the left offset tracks the measured caption advance.
    const int caption_advance =
        std::max(measure_label_width(window_, small_font_, L"原文"),
                 measure_label_width(window_, small_font_, L"译文"));
    const int action_left = edit_left + caption_advance + scaled(6, dpi_);
    const int action_offset = (label_height - action_height) / 2;
    MoveWindow(clear_button_, action_left, body_top + action_offset,
               action_width, action_height, TRUE);
    MoveWindow(copy_button_, action_left, divider_y + action_offset,
               action_width, action_height, TRUE);
}

void QuickTranslateWindow::paint() {
    PAINTSTRUCT paint{};
    HDC dc = BeginPaint(window_, &paint);
    RECT client{};
    GetClientRect(window_, &client);
    FillRect(dc, &client, background_);

    const int margin = scaled(16, dpi_);
    const int inset = scaled(13, dpi_);
    const int label_height = scaled(29, dpi_);
    const int header_top = scaled(10, dpi_);
    const int control_height = scaled(30, dpi_);

    const int body_top = header_top + control_height + scaled(14, dpi_);
    const int body_bottom = client.bottom - scaled(34, dpi_);
    RECT body{margin, body_top, client.right - margin, body_bottom};
    draw_rounded_control(dc, body, color_surface, color_border, scaled(10, dpi_));

    const int divider_y = body_top + (body_bottom - body_top) / 2;
    HPEN divider = CreatePen(PS_SOLID, 1, color_border);
    const HGDIOBJ previous_pen = SelectObject(dc, divider);
    MoveToEx(dc, body.left + inset, divider_y, nullptr);
    LineTo(dc, body.right - inset, divider_y);
    SelectObject(dc, previous_pen);
    DeleteObject(divider);

    const int content_left = body.left + inset;
    draw_label(dc, L"原文",
               RECT{content_left, body.top, content_left + scaled(60, dpi_),
                    body.top + label_height},
               small_font_, color_dim,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    draw_label(dc, L"译文",
               RECT{content_left, divider_y, content_left + scaled(60, dpi_),
                    divider_y + label_height},
               small_font_, color_dim,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    const bool copied = session_.copy_confirmation_visible();
    const std::wstring footer = copied
                                    ? std::wstring(L"已复制到剪贴板")
                                    : session_.state() == TextTranslationState::too_long
                                          ? std::wstring(L"已超出 500 字上限")
                                          : session_status(session_);
    draw_label(dc, footer,
               RECT{margin + scaled(2, dpi_), body.bottom + scaled(6, dpi_),
                    client.right - margin - scaled(200, dpi_),
                    client.bottom - scaled(8, dpi_)},
               small_font_,
               status_error_ ? color_error : color_dim,
               DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    draw_label(dc, L"Ctrl+Enter 翻译 · Esc 关闭",
               RECT{margin, body.bottom + scaled(6, dpi_), client.right - margin,
                    client.bottom - scaled(8, dpi_)},
               small_font_, color_faint,
               DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
    EndPaint(window_, &paint);
}

void QuickTranslateWindow::sync_input() {
    if (window_text(input_) == session_.input()) return;
    syncing_ = true;
    SetWindowTextW(input_, session_.input().c_str());
    syncing_ = false;
}

void QuickTranslateWindow::sync_source() {
    const int expected = target_index(session_.source(), false);
    if (SendMessageW(source_combo_, CB_GETCURSEL, 0, 0) == expected) return;
    syncing_ = true;
    SendMessageW(source_combo_, CB_SETCURSEL, std::max(0, expected), 0);
    syncing_ = false;
}

void QuickTranslateWindow::sync_target() {
    const int expected = target_index(session_.target(), false);
    if (SendMessageW(target_combo_, CB_GETCURSEL, 0, 0) == expected) return;
    syncing_ = true;
    SendMessageW(target_combo_, CB_SETCURSEL, expected, 0);
    syncing_ = false;
}

void QuickTranslateWindow::refresh() {
    if (!window_) return;
    sync_input();
    sync_source();
    sync_target();
    if (window_text(output_) != session_.output()) {
        SetWindowTextW(output_, session_.output().c_str());
    }
    status_error_ = session_.state() == TextTranslationState::error ||
                    session_.state() == TextTranslationState::too_long;
    EnableWindow(copy_button_, !session_.output().empty());
    EnableWindow(clear_button_, !session_.input().empty());
    InvalidateRect(input_, nullptr, FALSE);
    InvalidateRect(source_combo_, nullptr, FALSE);
    InvalidateRect(target_combo_, nullptr, FALSE);
    InvalidateRect(window_, nullptr, FALSE);
}

void QuickTranslateWindow::refresh_appearance() {
    if (!window_) return;
    create_theme_resources();
    // The action buttons follow the width of the "原文"/"译文" captions, so a
    // new UI font can move them; re-run the layout after rebuilding the fonts.
    layout_controls();
    for (HWND control : {source_combo_, swap_button_, target_combo_, input_, output_,
                         capture_button_, copy_button_, clear_button_}) {
        set_control_font(control, font_);
    }
    apply_dark_frame(window_, true);
    style_target_popup(source_combo_, dpi_);
    style_target_popup(target_combo_, dpi_);
    InvalidateRect(window_, nullptr, TRUE);
    InvalidateRect(source_combo_, nullptr, TRUE);
    InvalidateRect(target_combo_, nullptr, TRUE);
}

void QuickTranslateWindow::show(HWND owner) {
    show_at(owner, default_quick_anchor(), true);
}

void QuickTranslateWindow::show_at(HWND owner, POINT anchor, bool select_all) {
    create_window(owner);
    refresh();
    position_near_anchor(window_, anchor);
    ShowWindow(window_, SW_SHOW);
    SetForegroundWindow(window_);
    SetFocus(input_);
    if (select_all) SendMessageW(input_, EM_SETSEL, 0, -1);
}

void QuickTranslateWindow::toggle_near(HWND owner, POINT anchor, bool select_all) {
    if (visible()) hide();
    else show_at(owner, anchor, select_all);
}

void QuickTranslateWindow::hide() {
    const bool was_visible = window_ && IsWindowVisible(window_);
    if (window_) ShowWindow(window_, SW_HIDE);
    if (callbacks_.composition_changed) callbacks_.composition_changed(false);
    // A hidden window cannot show a translation that is still on the wire, so
    // let the host cancel it instead of letting it finish in the background.
    if (was_visible && callbacks_.hidden) callbacks_.hidden();
}

void QuickTranslateWindow::on_input_changed() {
    if (!syncing_ && callbacks_.input_changed) {
        callbacks_.input_changed(window_text(input_));
    }
}

void QuickTranslateWindow::on_source_changed() {
    if (syncing_) return;
    const std::wstring source = target_at(source_combo_, false);
    if (callbacks_.source_changed) callbacks_.source_changed(source);
}

void QuickTranslateWindow::on_target_changed() {
    if (syncing_) return;
    const std::wstring target = target_at(target_combo_, false);
    if (callbacks_.target_changed) callbacks_.target_changed(target);
}

void QuickTranslateWindow::swap_languages() {
    const std::wstring source = target_at(source_combo_, false);
    const std::wstring target = target_at(target_combo_, false);
    if (source.empty() || target.empty() || source == target) return;
    session_.set_source(target);
    session_.set_target(source);
    syncing_ = true;
    SendMessageW(source_combo_, CB_SETCURSEL, target_index(target, false), 0);
    SendMessageW(target_combo_, CB_SETCURSEL, target_index(source, false), 0);
    syncing_ = false;
    if (callbacks_.source_changed) callbacks_.source_changed(session_.source());
    if (callbacks_.target_changed) callbacks_.target_changed(session_.target());
    // Swapping the pair invalidates the text that was typed for the old
    // direction, so clear the editor instead of translating stale input.
    if (callbacks_.clear) callbacks_.clear();
    InvalidateRect(source_combo_, nullptr, FALSE);
    InvalidateRect(target_combo_, nullptr, FALSE);
    SetFocus(input_);
}

LRESULT QuickTranslateWindow::draw_item(const DRAWITEMSTRUCT& item) {
    if (item.CtlType == ODT_COMBOBOX && (item.itemState & ODS_COMBOBOXEDIT) != 0) {
        const bool source = GetDlgCtrlID(item.hwndItem) == id_source;
        fill_rect(item.hDC, item.rcItem, color_input);
        RECT text_bounds = item.rcItem;
        text_bounds.left += scaled(8, dpi_);
        text_bounds.right -= scaled(4, dpi_);
        draw_label(item.hDC,
                   source ? source_selector_text(session_)
                          : target_selector_text(session_, true),
                   text_bounds, small_font_, color_text,
                   DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        return TRUE;
    }
    return draw_owner_item(item, font_, accent_, icon_font_, action_icon_font_,
                           close_icon_font_);
}

void QuickTranslateWindow::draw_target_combo(HDC dc) {
    draw_target_selector(target_combo_, dc, small_font_, dpi_, accent_,
                         target_selector_text(session_, true));
}

void QuickTranslateWindow::draw_source_combo(HDC dc) {
    draw_target_selector(source_combo_, dc, small_font_, dpi_, accent_,
                         source_selector_text(session_));
}

bool QuickTranslateWindow::preprocess_message(MSG& message) {
    if (!visible() || !message_for(window_, message)) return false;
    if (message.message == WM_KEYDOWN && message.wParam == VK_ESCAPE) {
        hide();
        return true;
    }
    if (message.message == WM_KEYDOWN && message.wParam == VK_RETURN &&
        (GetKeyState(VK_CONTROL) & 0x8000)) {
        if (callbacks_.translate_now) callbacks_.translate_now();
        return true;
    }
    return IsDialogMessageW(window_, &message) != FALSE;
}

void QuickTranslateWindow::self_test(HWND owner) {
    create_window(owner);
    if (!input_ || !output_ || !target_combo_ || !source_combo_ || !swap_button_ ||
        !copy_button_ || !clear_button_ || !capture_button_ || !home_button_ ||
        SendMessageW(source_combo_, CB_GETCOUNT, 0, 0) != 9 ||
        SendMessageW(target_combo_, CB_GETCOUNT, 0, 0) != 9) {
        throw AppError("quick translation window self-test controls are incomplete");
    }
    const LONG_PTR ex_style = GetWindowLongPtrW(window_, GWL_EXSTYLE);
    if ((ex_style & WS_EX_TOOLWINDOW) == 0 || (ex_style & WS_EX_TOPMOST) == 0 ||
        (ex_style & WS_EX_APPWINDOW) != 0) {
        throw AppError("quick translation window self-test style is invalid");
    }
    if (dpi_ != static_cast<int>(GetDpiForWindow(window_))) {
        throw AppError("quick translation window self-test DPI mismatch");
    }
    RECT selector_client{};
    GetClientRect(target_combo_, &selector_client);
    HDC selector_dc = GetDC(target_combo_);
    HDC selector_test_dc = selector_dc ? CreateCompatibleDC(selector_dc) : nullptr;
    HBITMAP selector_bitmap = selector_dc
        ? CreateCompatibleBitmap(selector_dc,
                                 std::max(1L, selector_client.right),
                                 std::max(1L, selector_client.bottom))
        : nullptr;
    HGDIOBJ selector_previous = selector_test_dc && selector_bitmap
        ? SelectObject(selector_test_dc, selector_bitmap) : nullptr;
    bool closed_selector_painted = false;
    if (selector_test_dc && selector_previous) {
        RECT item_bounds = selector_client;
        item_bounds.right = std::max(item_bounds.left + 1,
                                     item_bounds.right - scaled(22, dpi_));
        DRAWITEMSTRUCT closed_item{};
        closed_item.CtlType = ODT_COMBOBOX;
        closed_item.CtlID = id_target;
        closed_item.itemID = 0;
        closed_item.itemAction = ODA_DRAWENTIRE;
        closed_item.itemState = ODS_COMBOBOXEDIT;
        closed_item.hwndItem = target_combo_;
        closed_item.hDC = selector_test_dc;
        closed_item.rcItem = item_bounds;
        draw_item(closed_item);
        closed_selector_painted =
            GetPixel(selector_test_dc, item_bounds.left, item_bounds.top) == color_input;
    }
    if (selector_previous) SelectObject(selector_test_dc, selector_previous);
    if (selector_bitmap) DeleteObject(selector_bitmap);
    if (selector_test_dc) DeleteDC(selector_test_dc);
    if (selector_dc) ReleaseDC(target_combo_, selector_dc);
    if (!closed_selector_painted) {
        throw AppError("quick translation window self-test closed target was not painted");
    }
    bool composition_started = false;
    bool composition_ended = false;
    auto composition_callback = callbacks_.composition_changed;
    callbacks_.composition_changed = [&](bool composing) {
        composition_started = composition_started || composing;
        composition_ended = composition_ended || !composing;
    };
    SendMessageW(input_, WM_IME_STARTCOMPOSITION, 0, 0);
    SendMessageW(input_, WM_IME_ENDCOMPOSITION, 0, 0);
    callbacks_.composition_changed = std::move(composition_callback);
    if (!composition_started || !composition_ended) {
        throw AppError("quick translation window self-test IME handling mismatch");
    }
    bool clear_called = false;
    auto clear_callback = callbacks_.clear;
    callbacks_.clear = [&] { clear_called = true; };
    SendMessageW(window_, WM_COMMAND, MAKEWPARAM(id_clear, BN_CLICKED),
                 reinterpret_cast<LPARAM>(clear_button_));
    callbacks_.clear = std::move(clear_callback);
    if (!clear_called || GetFocus() != input_) {
        throw AppError("quick translation window self-test clear button mismatch");
    }

    bool copy_source_called = false;
    auto copy_source_callback = callbacks_.copy;
    callbacks_.copy = [&] { copy_source_called = true; };
    SendMessageW(window_, WM_COMMAND, MAKEWPARAM(id_copy, BN_CLICKED),
                 reinterpret_cast<LPARAM>(copy_button_));
    callbacks_.copy = std::move(copy_source_callback);
    if (!copy_source_called) {
        throw AppError("quick translation window self-test copy button mismatch");
    }
    session_.set_source(L"fr");
    session_.set_target(L"ja");
    const std::wstring source_before_swap = session_.source();
    const std::wstring target_before_swap = session_.target();
    bool swap_called = false;
    std::wstring swapped_source;
    auto source_callback = callbacks_.source_changed;
    auto target_callback = callbacks_.target_changed;
    callbacks_.source_changed = [&](std::wstring value) {
        swap_called = true;
        swapped_source = value;
        session_.set_source(std::move(value));
    };
    callbacks_.target_changed = [&](std::wstring value) {
        session_.set_target(std::move(value));
    };
    SendMessageW(window_, WM_COMMAND, MAKEWPARAM(id_swap, BN_CLICKED),
                 reinterpret_cast<LPARAM>(swap_button_));
    callbacks_.source_changed = std::move(source_callback);
    callbacks_.target_changed = std::move(target_callback);
    if (!swap_called || swapped_source != target_before_swap ||
        session_.target() != source_before_swap || source_before_swap == target_before_swap) {
        throw AppError("quick translation window self-test swap mismatch");
    }
    bool capture_called = false;
    auto capture_callback = callbacks_.capture;
    callbacks_.capture = [&] { capture_called = true; };
    SendMessageW(window_, WM_COMMAND, MAKEWPARAM(id_quick_capture, BN_CLICKED),
                 reinterpret_cast<LPARAM>(capture_button_));
    callbacks_.capture = std::move(capture_callback);
    if (!capture_called || visible()) {
        throw AppError("quick translation window self-test capture button mismatch");
    }
    RECT capture_rect{};
    RECT home_rect{};
    RECT close_rect{};
    RECT source_combo_rect{};
    if (!GetWindowRect(capture_button_, &capture_rect) ||
        !GetWindowRect(home_button_, &home_rect) ||
        !GetWindowRect(close_button_, &close_rect) ||
        !GetWindowRect(source_combo_, &source_combo_rect)) {
        throw AppError("quick translation window self-test header buttons missing");
    }
    if (close_rect.right - close_rect.left != capture_rect.right - capture_rect.left ||
        close_rect.bottom - close_rect.top != capture_rect.bottom - capture_rect.top ||
        close_rect.top != capture_rect.top || close_rect.left <= home_rect.right ||
        home_rect.left <= capture_rect.right ||
        capture_rect.left <= source_combo_rect.right ||
        close_rect.right > source_combo_rect.left + scaled(480, dpi_)) {
        throw AppError("quick translation window self-test header layout mismatch");
    }
    show(owner);
    bool home_called = false;
    auto home_callback = callbacks_.home;
    callbacks_.home = [&] { home_called = true; };
    SendMessageW(window_, WM_COMMAND, MAKEWPARAM(id_quick_home, BN_CLICKED),
                 reinterpret_cast<LPARAM>(home_button_));
    callbacks_.home = std::move(home_callback);
    if (!home_called) {
        throw AppError("quick translation window self-test home button mismatch");
    }
    SendMessageW(window_, WM_COMMAND, MAKEWPARAM(id_quick_close, BN_CLICKED),
                 reinterpret_cast<LPARAM>(close_button_));
    if (visible()) {
        throw AppError("quick translation window self-test close button mismatch");
    }
    RECT copy_button_rect{};
    RECT clear_button_rect{};
    RECT quick_input_rect{};
    RECT quick_output_rect{};
    if (!GetWindowRect(copy_button_, &copy_button_rect) ||
        !GetWindowRect(clear_button_, &clear_button_rect) ||
        !GetWindowRect(input_, &quick_input_rect) ||
        !GetWindowRect(output_, &quick_output_rect)) {
        throw AppError("quick translation window self-test action buttons missing");
    }
    const int action_width = clear_button_rect.right - clear_button_rect.left;
    const int action_height = clear_button_rect.bottom - clear_button_rect.top;
    const int label_row_height = scaled(29, dpi_);
    if (clear_button_rect.left != copy_button_rect.left ||
        copy_button_rect.right - copy_button_rect.left != action_width ||
        copy_button_rect.bottom - copy_button_rect.top != action_height ||
        copy_button_rect.top <= clear_button_rect.bottom ||
        clear_button_rect.bottom > quick_input_rect.top ||
        clear_button_rect.top < quick_input_rect.top - label_row_height ||
        copy_button_rect.top < quick_input_rect.bottom ||
        copy_button_rect.bottom > quick_output_rect.top ||
        clear_button_rect.left < quick_input_rect.left ||
        clear_button_rect.left >= quick_input_rect.left + scaled(80, dpi_) ||
        copy_button_rect.right > quick_input_rect.right) {
        throw AppError("quick translation window self-test action buttons differ");
    }
    show(owner);
    RECT quick_client{};
    if (!GetClientRect(window_, &quick_client) ||
        quick_client.right < scaled(quick_window_logical_width, dpi_) / 2 ||
        quick_client.bottom < scaled(quick_window_logical_height, dpi_) / 2) {
        throw AppError("quick translation window self-test size mismatch");
    }
    RECT target_before_dropdown{};
    if (!GetWindowRect(target_combo_, &target_before_dropdown)) {
        throw AppError("quick translation window self-test target selector missing");
    }
    SendMessageW(target_combo_, CB_SHOWDROPDOWN, TRUE, 0);
    if (!SendMessageW(target_combo_, CB_GETDROPPEDSTATE, 0, 0)) {
        throw AppError("quick translation window self-test target menu did not open");
    }
    COMBOBOXINFO target_info{sizeof(target_info)};
    RECT target_list{};
    if (!GetComboBoxInfo(target_combo_, &target_info) || !target_info.hwndList ||
        !GetWindowRect(target_info.hwndList, &target_list) ||
        target_list.bottom - target_list.top < scaled(200, dpi_)) {
        throw AppError("quick translation window self-test target menu is clipped");
    }
    SendMessageW(target_combo_, CB_SHOWDROPDOWN, FALSE, 0);
    RECT target_after_dropdown{};
    if (!GetWindowRect(target_combo_, &target_after_dropdown) ||
        target_after_dropdown.left != target_before_dropdown.left ||
        target_after_dropdown.top != target_before_dropdown.top ||
        target_after_dropdown.right <= target_after_dropdown.left ||
        target_after_dropdown.bottom <= target_after_dropdown.top) {
        throw AppError("quick translation window self-test target selector resized");
    }
    SendMessageW(window_, WM_CLOSE, 0, 0);
    if (visible()) {
        throw AppError("quick translation window self-test close path did not hide");
    }
    show(owner);
    // The language selectors now sit on the left edge, so the draggable header
    // area is the gap between the last selector and the right-side buttons.
    RECT header_target{};
    RECT header_capture{};
    GetWindowRect(target_combo_, &header_target);
    GetWindowRect(capture_button_, &header_capture);
    const POINT drag_point{(header_target.right + header_capture.left) / 2,
                           (header_target.top + header_target.bottom) / 2};
    const auto drag_hit = SendMessageW(
        window_, WM_NCHITTEST, 0,
        MAKELPARAM(static_cast<short>(drag_point.x),
                   static_cast<short>(drag_point.y)));
    RECT input_bounds{};
    GetWindowRect(input_, &input_bounds);
    const POINT input_point{
        (input_bounds.left + input_bounds.right) / 2,
        (input_bounds.top + input_bounds.bottom) / 2,
    };
    const auto input_hit = SendMessageW(
        window_, WM_NCHITTEST, 0,
        MAKELPARAM(static_cast<short>(input_point.x),
                   static_cast<short>(input_point.y)));
    if (drag_hit != HTCAPTION || input_hit != HTCLIENT) {
        throw AppError("quick translation window self-test drag regions mismatch");
    }
    hide();
    for (int index = 0; index < 4; ++index) {
        show(owner);
        UpdateWindow(window_);
        UpdateWindow(target_combo_);
        hide();
    }
    settle_self_test_ui();
    // Six batches are the minimum resource_series_stable() accepts: three
    // warm-up batches plus three measured ones, so a leak still trips the
    // sustained-growth streak. Each batch shows the window eight times, which
    // is enough for a per-show leak to accumulate visibly.
    std::array<ProcessResourceSnapshot, 6> resource_samples{};
    resource_samples[0] = process_resources();
    for (std::size_t batch = 1; batch < resource_samples.size(); ++batch) {
        for (int index = 0; index < 8; ++index) {
            show(owner);
            UpdateWindow(window_);
            UpdateWindow(target_combo_);
            hide();
        }
        resource_samples[batch] = process_resources();
    }
    if (!resource_series_stable(resource_samples)) {
        throw AppError("quick translation window self-test leaked process resources" +
                       resource_change(resource_samples.front(),
                                       resource_samples.back()));
    }
    hide();
}

LRESULT CALLBACK QuickTranslateWindow::input_proc(HWND control, UINT message,
                                                  WPARAM wparam, LPARAM lparam,
                                                  UINT_PTR subclass_id, DWORD_PTR reference) {
    auto* self = reinterpret_cast<QuickTranslateWindow*>(reference);
    if (self && message == WM_IME_STARTCOMPOSITION && self->callbacks_.composition_changed) {
        self->callbacks_.composition_changed(true);
    } else if (self && message == WM_IME_ENDCOMPOSITION && self->callbacks_.composition_changed) {
        self->callbacks_.composition_changed(false);
    }
    if (self && (message == WM_SETFOCUS || message == WM_KILLFOCUS)) {
        InvalidateRect(self->window_, nullptr, FALSE);
    }
    if (message == WM_NCDESTROY) RemoveWindowSubclass(control, &QuickTranslateWindow::input_proc,
                                                       subclass_id);
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT CALLBACK QuickTranslateWindow::target_combo_proc(
        HWND control, UINT message, WPARAM wparam, LPARAM lparam,
        UINT_PTR subclass_id, DWORD_PTR reference) {
    auto* self = reinterpret_cast<QuickTranslateWindow*>(reference);
    switch (message) {
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        HDC dc = BeginPaint(control, &paint);
        if (self) {
            if (subclass_id == source_combo_subclass_id) {
                self->draw_source_combo(dc);
            } else {
                self->draw_target_combo(dc);
            }
        }
        EndPaint(control, &paint);
        return 0;
    }
    case WM_PRINTCLIENT:
        if (self) {
            if (subclass_id == source_combo_subclass_id) {
                self->draw_source_combo(reinterpret_cast<HDC>(wparam));
            } else {
                self->draw_target_combo(reinterpret_cast<HDC>(wparam));
            }
        }
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_SETFOCUS:
    case WM_KILLFOCUS: {
        const LRESULT result = DefSubclassProc(control, message, wparam, lparam);
        RedrawWindow(control, nullptr, nullptr,
                     RDW_INVALIDATE | RDW_UPDATENOW | RDW_NOERASE);
        return result;
    }
    case WM_NCDESTROY:
        RemoveWindowSubclass(control, &QuickTranslateWindow::target_combo_proc,
                             subclass_id);
        break;
    default:
        break;
    }
    return DefSubclassProc(control, message, wparam, lparam);
}

LRESULT CALLBACK QuickTranslateWindow::window_proc(HWND window, UINT message,
                                                   WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<QuickTranslateWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<const CREATESTRUCTW*>(lparam);
        self = static_cast<QuickTranslateWindow*>(create->lpCreateParams);
        self->window_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self) return DefWindowProcW(window, message, wparam, lparam);
    return self->handle_message(message, wparam, lparam);
}


}  // namespace minitrans
