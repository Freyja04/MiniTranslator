#pragma once

#include "update_window.hpp"

#include "theme.hpp"
#include "ui_draw.hpp"

#include <windows.h>

#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>

namespace minitrans {

struct UpdateWindow::WorkerDispatch {
    std::mutex mutex;
    HWND target{};
};

namespace update_internal {

constexpr wchar_t class_name[] = L"MiniTranslator.Native.UpdateWindow.v2";
constexpr int id_check = 8101;
constexpr int id_release = 8102;
constexpr int id_action = 8103;

using theme::color_bad;
using theme::color_card;
using theme::color_input;
using theme::color_line;
using theme::color_line_high;
using theme::color_ok;
using theme::color_page;
using theme::color_text;
using theme::color_text_dim;
using theme::color_text_faint;
using theme::color_warn;

using ui_draw::draw_text;
using ui_draw::set_font;
using ui_draw::text_of;

int px(int value, int dpi) noexcept;
void fill_round_rect(HDC dc, const RECT& rect, int radius,
                     COLORREF fill, COLORREF border);
std::wstring format_size(std::uint64_t bytes);
std::wstring format_published_at(std::wstring_view value);
std::wstring source_label(std::wstring_view repository);
bool show_install_confirmation(HINSTANCE instance, HWND owner,
                               std::wstring version);

}  // namespace update_internal

}  // namespace minitrans
