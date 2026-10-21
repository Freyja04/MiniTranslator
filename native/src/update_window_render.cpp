#include "update_window_internal.hpp"

#include "resource.h"
#include "theme.hpp"
#include "ui_draw.hpp"
#include "util.hpp"
#include "version.hpp"

#include <algorithm>
#include <cstdint>
#include <string>

namespace minitrans {

using namespace update_internal;

void UpdateWindow::paint() {
    PAINTSTRUCT state{};
    HDC target = BeginPaint(window_, &state);
    if (!target) {
        EndPaint(window_, &state);
        return;
    }
    RECT client{};
    GetClientRect(window_, &client);
    if (client.right <= client.left || client.bottom <= client.top) {
        EndPaint(window_, &state);
        return;
    }
    HDC dc = CreateCompatibleDC(target);
    HBITMAP bitmap = dc ? CreateCompatibleBitmap(target, client.right, client.bottom) : nullptr;
    if (!dc || !bitmap) {
        FillRect(target, &client, background_);
        if (bitmap) DeleteObject(bitmap);
        if (dc) DeleteDC(dc);
        EndPaint(window_, &state);
        return;
    }
    const auto old_bitmap = SelectObject(dc, bitmap);
    FillRect(dc, &client, background_);

    const COLORREF accent = theme::accent;
    const int margin = px(24, dpi_);

    RECT mark{margin, px(20, dpi_), margin + px(38, dpi_), px(58, dpi_)};
    const int icon_size = mark.right - mark.left;
    if (brand_icon_) {
        DrawIconEx(dc, mark.left, mark.top, brand_icon_, icon_size, icon_size, 0,
                   nullptr, DI_NORMAL);
    }

    const int heading_left = mark.right + px(12, dpi_);
    draw_text(dc, MINITRANS_DISPLAY_NAME,
              RECT{heading_left, px(17, dpi_), client.right - margin, px(42, dpi_)},
              title_font_, color_text,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);
    const auto channel = config_.string(L"updates.channel", L"stable") == L"stable"
        ? L"稳定版" : L"预览版";
    const auto version = L"当前版本 " + std::wstring(native_version) + L"  ·  " + channel;
    draw_text(dc, version,
              RECT{heading_left, px(42, dpi_), client.right - margin, px(62, dpi_)},
              small_font_, color_text_dim,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    COLORREF state_color = color_text_dim;
    std::wstring_view state_glyph = L"i";
    switch (view_state_) {
    case ViewState::checking: state_color = accent; state_glyph = L"↻"; break;
    case ViewState::current: state_color = color_ok; state_glyph = L"✓"; break;
    case ViewState::available: state_color = accent; state_glyph = L"i"; break;
    case ViewState::downloading: state_color = accent; state_glyph = L"↓"; break;
    case ViewState::ready: state_color = color_ok; state_glyph = L"✓"; break;
    case ViewState::installing: state_color = color_warn; state_glyph = L"i"; break;
    case ViewState::error: state_color = color_bad; state_glyph = L"!"; break;
    case ViewState::unconfigured: state_color = color_warn; state_glyph = L"!"; break;
    case ViewState::idle: break;
    }

    const int state_top = px(76, dpi_);
    const int state_height = px(progress_visible_ ? 100 : 76, dpi_);
    RECT state_card{margin, state_top, client.right - margin, state_top + state_height};
    fill_round_rect(dc, state_card, px(10, dpi_), color_card, color_line);
    RECT icon_rect{state_card.left + px(17, dpi_), state_card.top + px(15, dpi_),
                   state_card.left + px(49, dpi_), state_card.top + px(47, dpi_)};
    draw_text(dc, state_glyph, icon_rect, title_font_, state_color,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    const int state_text_left = state_card.left + px(63, dpi_);
    draw_text(dc, state_title_,
              RECT{state_text_left, state_card.top + px(11, dpi_),
                   state_card.right - px(17, dpi_), state_card.top + px(35, dpi_)},
              status_heading_font_, state_color,
              DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);

    if (progress_visible_) {
        RECT progress{state_text_left, state_card.bottom - px(23, dpi_),
                      state_card.right - px(17, dpi_), state_card.bottom - px(7, dpi_)};
        fill_round_rect(dc, progress, px(4, dpi_), color_input, color_line_high);
        if (progress_total_ > 0 && progress_received_ > 0) {
            const auto bounded = std::min(progress_received_, progress_total_);
            const int width = progress.right - progress.left;
            const int completed = static_cast<int>(
                std::min<std::uint64_t>(static_cast<std::uint64_t>(width),
                    bounded * static_cast<std::uint64_t>(width) / progress_total_));
            if (completed > 0) {
                RECT chunk{progress.left + 1, progress.top + 1,
                           std::min(progress.right - 1, progress.left + completed),
                           progress.bottom - 1};
                fill_round_rect(dc, chunk, px(3, dpi_), accent, accent);
            }
        }
    }

    if (details_visible_) {
        const int details_top = state_card.bottom + px(16, dpi_);
        RECT details{margin, details_top, client.right - margin, details_top + px(112, dpi_)};
        fill_round_rect(dc, details, px(10, dpi_), color_card, color_line);
        const int label_left = details.left + px(17, dpi_);
        const int value_left = details.left + px(96, dpi_);
        const int value_right = details.right - px(17, dpi_);
        int row = details.top + px(12, dpi_);
        const auto detail_key = [&](std::wstring_view key, int height) {
            draw_text(dc, key,
                      RECT{label_left, row, value_left - px(12, dpi_), row + px(height, dpi_)},
                      font_, color_text_dim, DT_LEFT | DT_TOP | DT_SINGLELINE);
            row += px(height + 7, dpi_);
        };
        if (!published_value_.empty()) detail_key(L"发布时间", 18);
        detail_key(L"安装包", 28);
        draw_text(dc, L"完整性",
                  RECT{label_left, row, value_left - px(12, dpi_), row + px(18, dpi_)},
                  font_, color_text_dim, DT_LEFT | DT_TOP | DT_SINGLELINE);
        draw_text(dc, verification_value_,
                  RECT{value_left, row, value_right, row + px(18, dpi_)},
                  font_, color_text_dim, DT_LEFT | DT_TOP | DT_SINGLELINE);
    }

    BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
    SelectObject(dc, old_bitmap);
    DeleteObject(bitmap);
    DeleteDC(dc);
    EndPaint(window_, &state);
}

LRESULT UpdateWindow::draw_item(const DRAWITEMSTRUCT& item) {
    if (item.CtlType != ODT_BUTTON) return FALSE;
    HDC dc = item.hDC;
    RECT rect = item.rcItem;
    const bool disabled = (item.itemState & ODS_DISABLED) != 0;
    const bool pressed = (item.itemState & ODS_SELECTED) != 0;
    const bool hot = (item.itemState & ODS_HOTLIGHT) != 0;
    const bool focused = (item.itemState & ODS_FOCUS) != 0;
    const COLORREF accent = theme::accent;
    ButtonRole role = ButtonRole::ghost;
    if (item.hwndItem == check_button_) role = check_role_;
    else if (item.hwndItem == release_button_) role = release_role_;
    else if (item.hwndItem == action_button_) role = action_role_;

    FillRect(dc, &rect, background_);
    COLORREF fill = color_page;
    COLORREF border = color_line_high;
    COLORREF foreground = color_text_dim;
    if (disabled) {
        fill = color_card;
        border = color_line;
        foreground = color_text_faint;
    } else if (role == ButtonRole::primary) {
        fill = pressed ? adjust_rgb(accent, -12) : hot ? adjust_rgb(accent, 18) : accent;
        border = fill;
        foreground = theme::accent_foreground;
    } else {
            fill = pressed ? theme::color_hover
                           : hot ? theme::color_hover : color_page;
        foreground = hot ? color_text : color_text_dim;
    }
    fill_round_rect(dc, rect, px(6, dpi_), fill, focused ? accent : border);

    std::wstring glyph = L"↻";
    if (item.hwndItem == release_button_) {
        glyph = L"◎";
    } else if (item.hwndItem == action_button_) {
        const auto label = text_of(item.hwndItem);
        if (label.find(L"取消") != std::wstring::npos) glyph = L"×";
        else if (label.find(L"安装") != std::wstring::npos ||
                 label.find(L"启动") != std::wstring::npos) glyph = L"✓";
        else glyph = L"↓";
    }
    const auto label = text_of(item.hwndItem);
    SIZE label_size{};
    const auto old_font = SelectObject(dc, font_);
    GetTextExtentPoint32W(dc, label.c_str(), static_cast<int>(label.size()), &label_size);
    SelectObject(dc, old_font);
    const int icon_width = px(16, dpi_);
    const int gap = px(6, dpi_);
    const int total = icon_width + gap + label_size.cx;
    const int button_width = static_cast<int>(rect.right - rect.left);
    const int left = static_cast<int>(rect.left) + std::max(0, (button_width - total) / 2);
    draw_text(dc, glyph,
              RECT{left, rect.top, left + icon_width, rect.bottom},
              font_, foreground, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    draw_text(dc, label,
              RECT{left + icon_width + gap, rect.top, rect.right - px(7, dpi_), rect.bottom},
              font_, foreground, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    if (focused) {
        RECT focus = rect;
        InflateRect(&focus, -px(3, dpi_), -px(3, dpi_));
        DrawFocusRect(dc, &focus);
    }
    return TRUE;
}

}  // namespace minitrans
