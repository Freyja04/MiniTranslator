#include "update_window_internal.hpp"

#include "resource.h"
#include "theme.hpp"
#include "ui_draw.hpp"
#include "util.hpp"

#include <commctrl.h>
#include <shellscalingapi.h>
#include <uxtheme.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cwchar>
#include <memory>
#include <string>
#include <string_view>

namespace minitrans {

namespace update_internal {

int px(int value, int dpi) noexcept {
    return MulDiv(value, dpi, 96);
}

void fill_round_rect(HDC dc, const RECT& rect, int radius,
                     COLORREF fill, COLORREF border) {
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    if (!brush || !pen) {
        if (pen) DeleteObject(pen);
        if (brush) DeleteObject(brush);
        return;
    }
    const auto old_brush = SelectObject(dc, brush);
    const auto old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom,
              radius * 2, radius * 2);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

using ui_draw::draw_text;

using ui_draw::set_font;
using ui_draw::text_of;

std::wstring format_size(std::uint64_t bytes) {
    constexpr std::array<std::wstring_view, 7> units{
        L"B", L"KB", L"MB", L"GB", L"TB", L"PB", L"EB",
    };
    double value = static_cast<double>(bytes);
    std::size_t unit = 0;
    while (value >= 1024.0 && unit + 1 < units.size()) {
        value /= 1024.0;
        ++unit;
    }
    wchar_t buffer[64]{};
    if (unit == 0) {
        swprintf_s(buffer, L"%llu B", static_cast<unsigned long long>(bytes));
    } else {
        swprintf_s(buffer, L"%.1f %s", value, units[unit].data());
    }
    return buffer;
}

std::wstring format_published_at(std::wstring_view value) {
    if (value.empty()) return {};
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    const std::wstring input(value);
    if (swscanf_s(input.c_str(), L"%d-%d-%dT%d:%d:%d",
                  &year, &month, &day, &hour, &minute, &second) != 6) {
        return input;
    }
    SYSTEMTIME parsed{};
    parsed.wYear = static_cast<WORD>(year);
    parsed.wMonth = static_cast<WORD>(month);
    parsed.wDay = static_cast<WORD>(day);
    parsed.wHour = static_cast<WORD>(hour);
    parsed.wMinute = static_cast<WORD>(minute);
    parsed.wSecond = static_cast<WORD>(second);

    const auto time_separator = input.find(L'T');
    const auto zone_position = input.find_last_of(L"Zz+-");
    const bool has_zone = zone_position != std::wstring::npos &&
                          time_separator != std::wstring::npos &&
                          zone_position > time_separator;
    SYSTEMTIME display = parsed;
    if (has_zone) {
        SYSTEMTIME utc = parsed;
        const wchar_t zone = input[zone_position];
        if (zone == L'+' || zone == L'-') {
            int zone_hour = 0;
            int zone_minute = 0;
            if (swscanf_s(input.c_str() + zone_position + 1, L"%d:%d",
                          &zone_hour, &zone_minute) != 2) {
                return input;
            }
            FILETIME file_time{};
            if (!SystemTimeToFileTime(&parsed, &file_time)) return input;
            ULARGE_INTEGER ticks{};
            ticks.LowPart = file_time.dwLowDateTime;
            ticks.HighPart = file_time.dwHighDateTime;
            const ULONGLONG offset = static_cast<ULONGLONG>(zone_hour * 60 + zone_minute) *
                                     60ULL * 10'000'000ULL;
            if (zone == L'+') {
                if (ticks.QuadPart < offset) return input;
                ticks.QuadPart -= offset;
            } else {
                ticks.QuadPart += offset;
            }
            file_time.dwLowDateTime = ticks.LowPart;
            file_time.dwHighDateTime = ticks.HighPart;
            if (!FileTimeToSystemTime(&file_time, &utc)) return input;
        }
        if (!SystemTimeToTzSpecificLocalTime(nullptr, &utc, &display)) return input;
    }

    wchar_t buffer[80]{};
    swprintf_s(buffer, has_zone ? L"%04u-%02u-%02u %02u:%02u（本地时间）"
                                : L"%04u-%02u-%02u %02u:%02u",
               display.wYear, display.wMonth, display.wDay,
               display.wHour, display.wMinute);
    return buffer;
}

std::wstring source_label(std::wstring_view repository) {
    if (repository.empty()) return L"本地开发构建";
    constexpr std::wstring_view prefix = L"https://github.com/";
    if (repository.starts_with(prefix)) {
        const auto owner_start = prefix.size();
        const auto slash = repository.find(L'/', owner_start);
        const auto owner = repository.substr(owner_start, slash - owner_start);
        if (!owner.empty()) return L"GitHub · " + std::wstring(owner);
    }
    return L"GitHub Release";
}

}  // namespace update_internal

using namespace update_internal;

void UpdateWindow::create_window(HWND owner) {
    WNDCLASSEXW description{};
    description.cbSize = sizeof(description);
    description.lpfnWndProc = &UpdateWindow::window_proc;
    description.style = CS_HREDRAW | CS_VREDRAW;
    description.hInstance = instance_;
    description.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    description.hIcon = LoadIconW(instance_, MAKEINTRESOURCEW(IDI_APP_ICON));
    description.hIconSm = description.hIcon;
    description.lpszClassName = class_name;
    if (!RegisterClassExW(&description) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS) {
        throw_last_error("register update window");
    }

    RECT owner_rect{};
    const bool usable_owner = owner && IsWindowVisible(owner) && GetWindowRect(owner, &owner_rect) &&
        owner_rect.right - owner_rect.left >= 100 && owner_rect.bottom - owner_rect.top >= 100;
    HMONITOR monitor{};
    if (usable_owner) {
        monitor = MonitorFromWindow(owner, MONITOR_DEFAULTTONEAREST);
    } else {
        POINT cursor{};
        GetCursorPos(&cursor);
        monitor = MonitorFromPoint(cursor, MONITOR_DEFAULTTOPRIMARY);
        MONITORINFO info{sizeof(info)};
        if (GetMonitorInfoW(monitor, &info)) {
            owner_rect = info.rcWork;
        } else {
            owner_rect = {0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        }
    }
    UINT monitor_dpi_x = 96;
    UINT monitor_dpi_y = 96;
    if (!monitor || FAILED(GetDpiForMonitor(
            monitor, MDT_EFFECTIVE_DPI, &monitor_dpi_x, &monitor_dpi_y))) {
        monitor_dpi_x = GetDpiForSystem();
    }
    dpi_ = usable_owner ? static_cast<int>(GetDpiForWindow(owner))
                        : static_cast<int>(monitor_dpi_x);

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU |
                        WS_THICKFRAME | WS_CLIPCHILDREN;
    constexpr DWORD ex_style = WS_EX_DLGMODALFRAME;
    RECT desired{0, 0, px(580, dpi_), px(260, dpi_)};
    AdjustWindowRectExForDpi(&desired, style, FALSE, ex_style, static_cast<UINT>(dpi_));
    const int width = desired.right - desired.left;
    const int height = desired.bottom - desired.top;
    const int x = owner_rect.left + std::max(0L, (owner_rect.right - owner_rect.left - width) / 2);
    const int y = owner_rect.top + std::max(0L, (owner_rect.bottom - owner_rect.top - height) / 2);
    window_error_.clear();
    auto dispatch = std::make_shared<WorkerDispatch>();
    const std::wstring window_title =
        std::wstring(MINITRANS_DISPLAY_NAME) + L" · 检查更新";
    window_ = CreateWindowExW(
        ex_style, class_name, window_title.c_str(), style,
        x, y, width, height, owner, nullptr, instance_, this);
    if (!window_) {
        if (!window_error_.empty()) throw AppError(window_error_);
        throw_last_error("create update window");
    }
    dispatch->target = window_;
    worker_dispatch_ = std::move(dispatch);
    theme::apply_window_theme(window_, true, true);
}

void UpdateWindow::create_theme_resources() {
    for (auto** font : {&font_, &title_font_, &status_heading_font_,
                        &small_font_}) {
        if (*font) DeleteObject(*font);
        *font = nullptr;
    }
    if (brand_icon_) {
        DestroyIcon(brand_icon_);
        brand_icon_ = nullptr;
    }
    if (background_) DeleteObject(background_);
    if (card_background_) DeleteObject(card_background_);
    background_ = nullptr;
    card_background_ = nullptr;

    const auto make_font = [&](int pixels, int weight) {
        return CreateFontW(-px(pixels, dpi_), 0, 0, 0, weight, FALSE, FALSE, FALSE,
                           DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                           CLEARTYPE_QUALITY, DEFAULT_PITCH,
                           theme::font_family.c_str());
    };
    font_ = make_font(theme::font_size_body, FW_NORMAL);
    title_font_ = make_font(theme::font_size_title, FW_SEMIBOLD);
    status_heading_font_ =
        make_font(theme::font_size_section, FW_SEMIBOLD);
    small_font_ = make_font(theme::font_size_small, FW_NORMAL);
    brand_icon_ = static_cast<HICON>(LoadImageW(
        instance_, MAKEINTRESOURCEW(IDI_APP_ICON), IMAGE_ICON, px(38, dpi_),
        px(38, dpi_), LR_DEFAULTCOLOR));
    background_ = CreateSolidBrush(color_page);
    card_background_ = CreateSolidBrush(color_card);
    if (!font_ || !title_font_ || !status_heading_font_ || !small_font_ ||
        !brand_icon_ || !background_ || !card_background_) {
        throw AppError("cannot create update window theme resources");
    }
}

void UpdateWindow::create_controls() {
    INITCOMMONCONTROLSEX common_controls{};
    common_controls.dwSize = sizeof(common_controls);
    common_controls.dwICC = ICC_WIN95_CLASSES;
    if (!InitCommonControlsEx(&common_controls)) {
        throw_last_error("initialize update common controls");
    }
    create_theme_resources();
    auto make_button = [&](const wchar_t* text, int identifier) {
        HWND button = CreateWindowExW(
            0, L"BUTTON", text,
            WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
            0, 0, 1, 1, window_,
            reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)), instance_, nullptr);
        if (!button) throw_last_error("create update button");
        set_font(button, font_);
        return button;
    };
    const auto make_selectable_text = [&](DWORD style, HFONT font) {
        HWND control = CreateWindowExW(
            0, L"EDIT", L"",
            WS_CHILD | WS_VISIBLE | ES_LEFT | ES_READONLY | style,
            0, 0, 1, 1, window_, nullptr, instance_, nullptr);
        if (!control) throw_last_error("create selectable update text");
        set_font(control, font);
        SendMessageW(control, EM_SETMARGINS,
                     EC_LEFTMARGIN | EC_RIGHTMARGIN, MAKELPARAM(0, 0));
    theme::apply_control_theme(control);
        return control;
    };
    check_button_ = make_button(L"检查更新", id_check);
    release_button_ = make_button(L"发布说明", id_release);
    action_button_ = make_button(L"下载并安装", id_action);
    state_message_control_ = make_selectable_text(ES_MULTILINE, small_font_);
    published_value_control_ = make_selectable_text(0, font_);
    artifact_value_control_ = make_selectable_text(ES_MULTILINE, font_);
    source_control_ = make_selectable_text(0, small_font_);
    ShowWindow(release_button_, SW_HIDE);
    ShowWindow(action_button_, SW_HIDE);

    repository_tooltip_ = CreateWindowExW(
        WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
        WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
        CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
        window_, nullptr, instance_, nullptr);
    if (!repository_tooltip_) throw_last_error("create repository tooltip");
    theme::apply_control_theme(repository_tooltip_);
    SendMessageW(repository_tooltip_, TTM_SETTIPBKCOLOR, color_input, 0);
    SendMessageW(repository_tooltip_, TTM_SETTIPTEXTCOLOR, color_text, 0);
    SendMessageW(repository_tooltip_, TTM_SETMAXTIPWIDTH, 0, px(640, dpi_));
    SetWindowPos(repository_tooltip_, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    repository_tooltip_text_ = config_.string(L"updates.repository_url");
    TOOLINFOW tool{};
    tool.cbSize = TTTOOLINFOW_V2_SIZE;
    tool.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
    tool.hwnd = source_control_;
    tool.uId = 1;
    GetClientRect(source_control_, &tool.rect);
    tool.lpszText = repository_tooltip_text_.data();
    if (!SendMessageW(repository_tooltip_, TTM_ADDTOOLW, 0,
                      reinterpret_cast<LPARAM>(&tool))) {
        throw AppError("cannot attach repository tooltip");
    }
    refresh_repository_control();
    layout_controls();
}

void UpdateWindow::sync_text_controls() {
    const auto set_text = [](HWND control, const std::wstring& value) {
        if (control && text_of(control) != value) {
            SetWindowTextW(control, value.c_str());
        }
    };
    set_text(state_message_control_, state_message_);
    set_text(published_value_control_, published_value_);
    set_text(artifact_value_control_, artifact_value_);
}

void UpdateWindow::refresh_repository_control() {
    if (!source_control_) return;
    const auto repository = config_.string(L"updates.repository_url");
    const auto label = source_label(repository);
    if (text_of(source_control_) != label) {
        SetWindowTextW(source_control_, label.c_str());
    }
    repository_tooltip_text_ = repository;
    if (!repository_tooltip_) return;
    TOOLINFOW tool{};
    tool.cbSize = TTTOOLINFOW_V2_SIZE;
    tool.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
    tool.hwnd = source_control_;
    tool.uId = 1;
    GetClientRect(source_control_, &tool.rect);
    tool.lpszText = repository_tooltip_text_.data();
    SendMessageW(repository_tooltip_, TTM_UPDATETIPTEXTW, 0,
                 reinterpret_cast<LPARAM>(&tool));
}

void UpdateWindow::layout_controls() {
    if (!window_) return;
    sync_text_controls();
    RECT client{};
    GetClientRect(window_, &client);
    const int margin = px(24, dpi_);
    const int button_height = px(36, dpi_);
    const int button_y = client.bottom - px(20, dpi_) - button_height;
    int x = client.right - margin;
    auto place = [&](HWND button, int width) {
        if (!button || !IsWindowVisible(button)) return;
        x -= px(width, dpi_);
        MoveWindow(button, x, button_y, px(width, dpi_), button_height, TRUE);
        x -= px(9, dpi_);
    };
    place(action_button_, 132);
    place(release_button_, 104);
    place(check_button_, 112);

    const int state_top = px(76, dpi_);
    const int state_height = px(progress_visible_ ? 100 : 76, dpi_);
    const int state_text_left = margin + px(63, dpi_);
    const int state_right = client.right - margin - px(17, dpi_);
    const int state_message_top = state_top + px(34, dpi_);
    const int state_message_bottom = state_top + state_height -
                                     px(progress_visible_ ? 29 : 9, dpi_);
    MoveWindow(state_message_control_, state_text_left, state_message_top,
               std::max(1, state_right - state_text_left),
               std::max(1, state_message_bottom - state_message_top), TRUE);

    if (details_visible_) {
        const int details_top = state_top + state_height + px(16, dpi_);
        const int value_left = margin + px(96, dpi_);
        const int value_right = client.right - margin - px(17, dpi_);
        int row = details_top + px(12, dpi_);
        if (!published_value_.empty()) {
            MoveWindow(published_value_control_, value_left, row,
                       std::max(1, value_right - value_left), px(18, dpi_), TRUE);
            ShowWindow(published_value_control_, SW_SHOW);
            row += px(25, dpi_);
        } else {
            ShowWindow(published_value_control_, SW_HIDE);
        }
        MoveWindow(artifact_value_control_, value_left, row,
                   std::max(1, value_right - value_left), px(28, dpi_), TRUE);
        ShowWindow(artifact_value_control_, SW_SHOW);
    } else {
        ShowWindow(published_value_control_, SW_HIDE);
        ShowWindow(artifact_value_control_, SW_HIDE);
    }

    const int actions_top = client.bottom - px(56, dpi_);
    MoveWindow(source_control_, margin, actions_top,
               std::max(1, x - margin), px(36, dpi_), TRUE);
    if (repository_tooltip_) {
        TOOLINFOW tool{};
        tool.cbSize = TTTOOLINFOW_V2_SIZE;
        tool.uFlags = TTF_SUBCLASS | TTF_TRANSPARENT;
        tool.hwnd = source_control_;
        tool.uId = 1;
        GetClientRect(source_control_, &tool.rect);
        SendMessageW(repository_tooltip_, TTM_NEWTOOLRECTW, 0,
                     reinterpret_cast<LPARAM>(&tool));
    }
}

void UpdateWindow::resize_for_content() {
    if (!window_) return;
    const int client_height = details_visible_
        ? (progress_visible_ ? 382 : 360)
        : (progress_visible_ ? 282 : 260);
    RECT current_client{};
    RECT window_rect{};
    GetClientRect(window_, &current_client);
    GetWindowRect(window_, &window_rect);
    RECT desired{0, 0, current_client.right, px(client_height, dpi_)};
    AdjustWindowRectExForDpi(
        &desired,
        static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_STYLE)), FALSE,
        static_cast<DWORD>(GetWindowLongPtrW(window_, GWL_EXSTYLE)),
        static_cast<UINT>(dpi_));
    SetWindowPos(window_, nullptr, window_rect.left, window_rect.top,
                 window_rect.right - window_rect.left, desired.bottom - desired.top,
                 SWP_NOACTIVATE | SWP_NOZORDER);
}

}  // namespace minitrans
