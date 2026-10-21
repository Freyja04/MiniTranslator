#include "result_window.hpp"
#include "result_window_internal.hpp"

#include "theme.hpp"
#include "util.hpp"

#include <commctrl.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <cstdint>

namespace minitrans {

namespace {

constexpr wchar_t control_bar_class_name[] = L"MiniTranslator.Native.ResultControlBar.v1";
// Keep the detached result toolbar dimensionally identical to the Python
// reference.  At 26 px the native toolbar covered noticeably more of the
// surrounding desktop and made the glyphs read heavier than the original.
constexpr int logical_button_size = 20;
constexpr int logical_bar_padding = 4;
constexpr int logical_bar_gap = 5;
constexpr int logical_grip_width = 10;
constexpr int logical_button_spacing = 2;
constexpr std::array<int, 4> command_ids{4103, 4102, 4104, 4105};
constexpr std::array<const wchar_t*, 4> button_tips{
    L"校对识别文字（E）", L"用当前接口重新翻译（R）",
    L"缩到托盘（M）", L"关闭（Esc）",
};

}  // namespace

using result_window_internal::d2d_color;
using result_window_internal::d2d_factory;
using result_window_internal::dpi_value;
using result_window_internal::draw_control_icon;

void ResultWindow::create_control_bar() {
    if (control_bar_) return;
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = &ResultWindow::control_bar_proc;
    description.style = 0;
    description.hInstance = instance_;
    description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    description.hbrBackground = nullptr;
    description.lpszClassName = control_bar_class_name;
    const ATOM atom = RegisterClassExW(&description);
    if (!atom && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw_last_error("register result control bar");
    }
    control_bar_ = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_LAYERED,
        control_bar_class_name, L"结果控制", WS_POPUP,
        0, 0, 1, 1, window_, nullptr, instance_, this);
    if (!control_bar_) throw_last_error("create result control bar");
    update_control_bar_region();

    control_bar_tooltip_ = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        control_bar_, nullptr, instance_, nullptr);
    if (control_bar_tooltip_) {
        theme::apply_control_theme(control_bar_tooltip_);
        SendMessageW(control_bar_tooltip_, TTM_SETTIPBKCOLOR,
                     static_cast<WPARAM>(theme::color_card), 0);
        SendMessageW(control_bar_tooltip_, TTM_SETTIPTEXTCOLOR,
                     static_cast<WPARAM>(theme::color_text), 0);
        SetWindowPos(control_bar_tooltip_, HWND_TOPMOST, 0, 0, 0, 0,
                     SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        for (std::size_t index = 0; index < command_ids.size(); ++index) {
            TOOLINFOW tool{};
            tool.cbSize = TTTOOLINFOW_V2_SIZE;
            tool.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
            tool.hwnd = control_bar_;
            tool.uId = static_cast<UINT_PTR>(index + 1);
            tool.rect = control_bar_button_rect(static_cast<int>(index));
            tool.lpszText = const_cast<wchar_t*>(button_tips[index]);
            SendMessageW(control_bar_tooltip_, TTM_ADDTOOLW, 0,
                         reinterpret_cast<LPARAM>(&tool));
        }
        RECT client{};
        GetClientRect(control_bar_, &client);
        TOOLINFOW grip_tool{};
        grip_tool.cbSize = TTTOOLINFOW_V2_SIZE;
        grip_tool.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
        grip_tool.hwnd = control_bar_;
        grip_tool.uId = 100;
        grip_tool.rect = {0, 0, control_bar_button_rect(0).left, client.bottom};
        grip_tool.lpszText = const_cast<wchar_t*>(L"拖动这里可以移动窗口");
        SendMessageW(control_bar_tooltip_, TTM_ADDTOOLW, 0,
                     reinterpret_cast<LPARAM>(&grip_tool));
    }
}

void ResultWindow::destroy_control_bar() {
    if (control_bar_tooltip_) {
        DestroyWindow(control_bar_tooltip_);
        control_bar_tooltip_ = nullptr;
    }
    if (control_bar_) {
        const HWND old = control_bar_;
        DestroyWindow(old);
        if (control_bar_ == old) control_bar_ = nullptr;
    }
    control_bar_hover_ = -1;
    control_bar_pressed_ = -1;
    control_bar_dragging_ = false;
}

RECT ResultWindow::control_bar_button_rect(int index) const {
    const int padding = dpi_value(window_, logical_bar_padding);
    const int grip = dpi_value(window_, logical_grip_width);
    const int button = dpi_value(window_, logical_button_size);
    const int spacing = dpi_value(window_, logical_button_spacing);
    const int left = padding + grip + index * (button + spacing);
    return {left, padding, left + button, padding + button};
}

int ResultWindow::control_bar_button_at(POINT point) const {
    for (int index = 0; index < static_cast<int>(command_ids.size()); ++index) {
        const RECT bounds = control_bar_button_rect(index);
        if (PtInRect(&bounds, point)) return index;
    }
    return -1;
}

void ResultWindow::update_control_bar_region() {
    if (!control_bar_) return;
    const int padding = dpi_value(window_, logical_bar_padding);
    const int grip = dpi_value(window_, logical_grip_width);
    const int button = dpi_value(window_, logical_button_size);
    const int spacing = dpi_value(window_, logical_button_spacing);
    const int width = padding + grip + button * static_cast<int>(command_ids.size())
                    + spacing * (static_cast<int>(command_ids.size()) - 1) + padding;
    const int height = padding * 2 + button;
    SetWindowPos(control_bar_, nullptr, 0, 0, width, height,
                 SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    SetWindowRgn(control_bar_, nullptr, FALSE);

    if (control_bar_tooltip_) {
        for (std::size_t index = 0; index < command_ids.size(); ++index) {
            TOOLINFOW tool{};
            tool.cbSize = TTTOOLINFOW_V2_SIZE;
            tool.hwnd = control_bar_;
            tool.uId = static_cast<UINT_PTR>(index + 1);
            tool.rect = control_bar_button_rect(static_cast<int>(index));
            SendMessageW(control_bar_tooltip_, TTM_NEWTOOLRECTW, 0,
                         reinterpret_cast<LPARAM>(&tool));
        }
        RECT client{};
        GetClientRect(control_bar_, &client);
        TOOLINFOW grip_tool{};
        grip_tool.cbSize = TTTOOLINFOW_V2_SIZE;
        grip_tool.hwnd = control_bar_;
        grip_tool.uId = 100;
        grip_tool.rect = {0, 0, control_bar_button_rect(0).left, client.bottom};
        SendMessageW(control_bar_tooltip_, TTM_NEWTOOLRECTW, 0,
                     reinterpret_cast<LPARAM>(&grip_tool));
    }
}

void ResultWindow::sync_control_bar() {
    if (!window_ || !control_bar_ || !IsWindowVisible(window_)) return;
    RECT result_rect{};
    RECT bar_rect{};
    GetWindowRect(window_, &result_rect);
    GetWindowRect(control_bar_, &bar_rect);
    const int bar_width = bar_rect.right - bar_rect.left;
    const int bar_height = bar_rect.bottom - bar_rect.top;
    const int gap = dpi_value(window_, logical_bar_gap);

    MONITORINFO monitor{};
    monitor.cbSize = sizeof(monitor);
    GetMonitorInfoW(MonitorFromRect(&result_rect, MONITOR_DEFAULTTONEAREST), &monitor);
    int x = result_rect.right - bar_width;
    int y = result_rect.bottom + gap;
    if (y + bar_height > monitor.rcWork.bottom) {
        y = result_rect.bottom - bar_height - gap;
    }
    x = std::clamp(x, static_cast<int>(monitor.rcWork.left),
                   static_cast<int>(monitor.rcWork.right) - bar_width);
    y = std::max(static_cast<int>(monitor.rcWork.top), y);
    SetWindowPos(control_bar_, HWND_TOPMOST, x, y, bar_width, bar_height,
                 SWP_NOACTIVATE | SWP_SHOWWINDOW);
}

void ResultWindow::paint_control_bar() {
    if (!control_bar_) return;
    PAINTSTRUCT state{};
    if (!BeginPaint(control_bar_, &state)) return;
    RECT client{};
    GetClientRect(control_bar_, &client);
    const int width = client.right - client.left;
    const int height = client.bottom - client.top;
    HDC screen = GetDC(nullptr);
    HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;
    BITMAPINFO info{};
    info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth = width;
    info.bmiHeader.biHeight = -height;
    info.bmiHeader.biPlanes = 1;
    info.bmiHeader.biBitCount = 32;
    info.bmiHeader.biCompression = BI_RGB;
    void* bits = nullptr;
    HBITMAP bitmap = memory && width > 0 && height > 0
        ? CreateDIBSection(screen, &info, DIB_RGB_COLORS, &bits, nullptr, 0)
        : nullptr;
    HGDIOBJ previous_bitmap = bitmap && memory ? SelectObject(memory, bitmap) : nullptr;
    bool rendered = false;
    ID2D1DCRenderTarget* target = nullptr;
    if (bits && previous_bitmap && previous_bitmap != HGDI_ERROR) {
        std::fill_n(static_cast<std::uint8_t*>(bits),
                    static_cast<std::size_t>(width) * height * 4,
                    std::uint8_t{0});
    }
    if (auto* factory = d2d_factory(); factory && bits && previous_bitmap
        && previous_bitmap != HGDI_ERROR) {
        D2D1_RENDER_TARGET_PROPERTIES properties{};
        properties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
        properties.usage = D2D1_RENDER_TARGET_USAGE_NONE;
        properties.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
        if (SUCCEEDED(factory->CreateDCRenderTarget(&properties, &target)) &&
            SUCCEEDED(target->BindDC(memory, &client))) {
            target->BeginDraw();
            target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            target->Clear(D2D1::ColorF(0.0F, 0.0F, 0.0F, 0.0F));

            ID2D1SolidColorBrush* shadow = nullptr;
            ID2D1SolidColorBrush* background = nullptr;
            ID2D1SolidColorBrush* edge = nullptr;
            target->CreateSolidColorBrush(d2d_color(RGB(0, 0, 0), 46.0F / 255.0F),
                                          &shadow);
            target->CreateSolidColorBrush(d2d_color(theme::color_card, 242.0F / 255.0F),
                                          &background);
            target->CreateSolidColorBrush(d2d_color(theme::color_line, 120.0F / 255.0F),
                                          &edge);
            const float radius = static_cast<float>(height) * 0.5F;
            const auto pill = D2D1::RoundedRect(
                D2D1::RectF(0.5F, 0.5F, static_cast<float>(width) - 0.5F,
                            static_cast<float>(height) - 0.5F),
                radius, radius);
            const auto pill_shadow = D2D1::RoundedRect(
                D2D1::RectF(-0.1F, 0.7F, static_cast<float>(width) + 0.1F,
                            static_cast<float>(height) + 0.9F),
                radius, radius);
            if (shadow) target->FillRoundedRectangle(pill_shadow, shadow);
            if (background) target->FillRoundedRectangle(pill, background);
            if (edge) target->DrawRoundedRectangle(pill, edge, 1.0F);
            if (shadow) shadow->Release();
            if (background) background->Release();
            if (edge) edge->Release();

            ID2D1SolidColorBrush* dots = nullptr;
            target->CreateSolidColorBrush(d2d_color(theme::color_text_dim, 130.0F / 255.0F),
                                          &dots);
            if (dots) {
                const float scale = static_cast<float>(GetDpiForWindow(window_)) / 96.0F;
                const float cx = static_cast<float>(dpi_value(window_, logical_bar_padding))
                               + static_cast<float>(dpi_value(window_, logical_grip_width)) * 0.5F;
                const float cy = static_cast<float>(height) * 0.5F;
                for (const float dy : {-3.4F, 0.0F, 3.4F}) {
                    for (const float dx : {-1.7F, 1.7F}) {
                        target->FillEllipse(
                            D2D1::Ellipse(D2D1::Point2F(cx + dx * scale, cy + dy * scale),
                                         0.8F * scale, 0.8F * scale),
                            dots);
                    }
                }
                dots->Release();
            }

            for (int index = 0; index < static_cast<int>(command_ids.size()); ++index) {
                const RECT bounds = control_bar_button_rect(index);
                const bool enabled = index == 0
                    ? (editable_result_ && !loading_ && !refreshing_ &&
                       !pending_source_ && !editing_)
                    : true;
                const bool hovered = enabled && control_bar_hover_ == index;
                const bool pressed = enabled && control_bar_pressed_ == index;
                if (hovered || pressed) {
                    ID2D1SolidColorBrush* hover = nullptr;
                    if (index == 3) {
                        target->CreateSolidColorBrush(
                            d2d_color(RGB(232, 82, 72), hovered ? 1.0F : 205.0F / 255.0F),
                            &hover);
                    } else {
                        target->CreateSolidColorBrush(
                            d2d_color(theme::color_text,
                                      pressed ? 74.0F / 255.0F : 46.0F / 255.0F),
                            &hover);
                    }
                    if (hover) {
                        const auto button = D2D1::RoundedRect(
                            D2D1::RectF(static_cast<float>(bounds.left) + 0.5F,
                                        static_cast<float>(bounds.top) + 0.5F,
                                        static_cast<float>(bounds.right) - 0.5F,
                                        static_cast<float>(bounds.bottom) - 0.5F),
                            static_cast<float>(dpi_value(window_, 5)),
                            static_cast<float>(dpi_value(window_, 5)));
                        target->FillRoundedRectangle(button, hover);
                        hover->Release();
                    }
                }
                const float inset = static_cast<float>(dpi_value(window_, 3));
                draw_control_icon(
                    target, index,
                    D2D1::RectF(static_cast<float>(bounds.left) + inset,
                                static_cast<float>(bounds.top) + inset,
                                static_cast<float>(bounds.right) - inset,
                                static_cast<float>(bounds.bottom) - inset),
                    theme::color_text,
                    hovered ? 245.0F / 255.0F
                            : enabled ? 200.0F / 255.0F : 82.0F / 255.0F);
            }
            rendered = SUCCEEDED(target->EndDraw());
        }
    }
    if (target) target->Release();

    if (rendered && screen && memory) {
        RECT window_rect{};
        GetWindowRect(control_bar_, &window_rect);
        POINT destination{window_rect.left, window_rect.top};
        POINT source{0, 0};
        SIZE size{width, height};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        if (!UpdateLayeredWindow(control_bar_, screen, &destination, &size,
                                 memory, &source, 0, &blend, ULW_ALPHA)) {
            OutputDebugStringW(L"MiniTranslator: could not update result control bar.\n");
        }
    }
    if (memory && previous_bitmap && previous_bitmap != HGDI_ERROR) {
        SelectObject(memory, previous_bitmap);
    }
    if (bitmap) DeleteObject(bitmap);
    if (memory) DeleteDC(memory);
    if (screen) ReleaseDC(nullptr, screen);
    EndPaint(control_bar_, &state);
}

LRESULT CALLBACK ResultWindow::control_bar_proc(HWND window, UINT message,
                                                 WPARAM wparam, LPARAM lparam) {
    auto* self = reinterpret_cast<ResultWindow*>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        const auto* create = reinterpret_cast<CREATESTRUCTW*>(lparam);
        self = static_cast<ResultWindow*>(create->lpCreateParams);
        self->control_bar_ = window;
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    return self ? self->handle_control_bar_message(message, wparam, lparam)
                : DefWindowProcW(window, message, wparam, lparam);
}

LRESULT ResultWindow::handle_control_bar_message(UINT message, WPARAM wparam, LPARAM lparam) {
    switch (message) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint_control_bar(); return 0;
    case WM_MOUSEACTIVATE: return MA_NOACTIVATE;
    case WM_SETCURSOR: {
        POINT cursor{};
        GetCursorPos(&cursor);
        ScreenToClient(control_bar_, &cursor);
        if (control_bar_dragging_ || control_bar_button_at(cursor) < 0) {
            SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
            return TRUE;
        }
        break;
    }
    case WM_MOUSEMOVE: {
        if (control_bar_dragging_) {
            POINT cursor{};
            if (GetCursorPos(&cursor)) {
                move_by(cursor.x - control_bar_drag_cursor_.x,
                        cursor.y - control_bar_drag_cursor_.y);
                control_bar_drag_cursor_ = cursor;
            }
            return 0;
        }
        const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        const int hovered = control_bar_button_at(point);
        if (hovered != control_bar_hover_) {
            control_bar_hover_ = hovered;
            InvalidateRect(control_bar_, nullptr, FALSE);
        }
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, control_bar_, 0};
        TrackMouseEvent(&track);
        return 0;
    }
    case WM_MOUSELEAVE:
        if (!control_bar_dragging_ && control_bar_hover_ != -1) {
            control_bar_hover_ = -1;
            InvalidateRect(control_bar_, nullptr, FALSE);
        }
        return 0;
    case WM_LBUTTONDOWN: {
        const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        control_bar_pressed_ = control_bar_button_at(point);
        SetCapture(control_bar_);
        if (control_bar_pressed_ < 0) {
            const bool can_drag = !recapturing_ && !resizing_;
            control_bar_dragging_ = can_drag &&
                GetCursorPos(&control_bar_drag_cursor_) != FALSE;
            if (control_bar_dragging_) SetCursor(LoadCursorW(nullptr, IDC_SIZEALL));
        }
        InvalidateRect(control_bar_, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONUP: {
        const bool dragged = control_bar_dragging_;
        const int pressed = control_bar_pressed_;
        control_bar_dragging_ = false;
        control_bar_pressed_ = -1;
        if (GetCapture() == control_bar_) ReleaseCapture();
        const POINT point{GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam)};
        const int released = control_bar_button_at(point);
        InvalidateRect(control_bar_, nullptr, FALSE);
        if (!dragged && pressed >= 0 && pressed == released) {
            const bool enabled = pressed == 0
                ? (editable_result_ && !loading_ && !refreshing_ &&
                   !pending_source_ && !editing_)
                : pressed != 1 || (!loading_ && !refreshing_);
            const bool actionable = enabled &&
                (pressed != 1 || (!loading_ && !refreshing_));
            if (actionable && window_) {
                PostMessageW(window_, WM_COMMAND,
                             MAKEWPARAM(command_ids[static_cast<std::size_t>(pressed)], 0), 0);
            }
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        control_bar_dragging_ = false;
        control_bar_pressed_ = -1;
        InvalidateRect(control_bar_, nullptr, FALSE);
        return 0;
    case WM_DPICHANGED:
        update_control_bar_region();
        sync_control_bar();
        return 0;
    case WM_NCDESTROY: {
        const HWND old = control_bar_;
        SetWindowLongPtrW(old, GWLP_USERDATA, 0);
        control_bar_ = nullptr;
        return DefWindowProcW(old, message, wparam, lparam);
    }
    default: break;
    }
    return DefWindowProcW(control_bar_, message, wparam, lparam);
}

}  // namespace minitrans
