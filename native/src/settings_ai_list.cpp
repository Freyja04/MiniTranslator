#include "settings_ai_list.hpp"

#include "util.hpp"

#include <algorithm>

namespace minitrans::settings_ai_list {
namespace {

// The row is tall enough to hold its 26px buttons and a hairline separator at
// the bottom, and the stride is the item height itself so list-box item
// rectangles, painting and hit testing agree without a second set of numbers.
constexpr int logical_height = 44;
constexpr int logical_padding = 12;
constexpr int logical_button_height = 26;
constexpr int logical_button_gap = 4;
constexpr int logical_toggle_width = 30;
constexpr int logical_edit_width = 30;
constexpr int logical_remove_width = 30;
constexpr int logical_narrow_width = 26;
constexpr int logical_name_reserve = 96;

}  // namespace

std::vector<Row> make_rows(const std::vector<settings_profiles::Profile>& list) {
    std::vector<Row> rows;
    rows.reserve(list.size());
    for (const auto& profile : list) {
        rows.push_back(Row{profile.name, profile.enabled,
                           settings_profiles::is_configured(profile)});
    }
    return rows;
}

int logical_row_height() noexcept {
    return logical_height;
}

int logical_row_padding() noexcept {
    return logical_padding;
}

std::wstring enable_label(const Row& row) noexcept {
    return row.enabled ? L"停用" : L"启用";
}

int scaled(int logical, int dpi) noexcept {
    return MulDiv(logical, dpi > 0 ? dpi : 96, 96);
}

std::vector<std::pair<Button, RECT>> buttons(const RECT& row, int dpi, bool* tight) {
    return buttons(row, dpi, nullptr, tight);
}

std::vector<std::pair<Button, RECT>> buttons(const RECT& row, int dpi,
                                             const Row* row_state, bool* tight) {
    if (tight) *tight = false;
    if (row.right <= row.left || row.bottom <= row.top) return {};
    const int gap = scaled(logical_button_gap, dpi);
    const int height = scaled(logical_button_height, dpi);
    // Buttons hug the row's right edge with only a hair of margin, so the
    // toggle lines up with the 添加 circle at the same column; the name keeps
    // the full padding on the left side.
    const int content_right = row.right - scaled(4, dpi);
    // RECT members are LONG, which is not the type std::max already sees in the
    // button height, so the row height is widened before it is compared.
    const int row_height = static_cast<int>(row.bottom - row.top);
    const int top = row.top + std::max(0, (row_height - height) / 2);
    const int bottom = top + height;

    const Row::Actions kind = row_state ? row_state->actions
                                        : Row::Actions::full;
    // The narrow layout swaps the text captions for icons when a row cannot
    // hold all its buttons and still leave room for a readable name.
    int wide_total = 0;
    if (kind == Row::Actions::full) {
        wide_total = scaled(logical_toggle_width + logical_edit_width +
                                logical_remove_width + logical_button_gap * 2,
                            dpi);
    } else if (kind == Row::Actions::toggle_edit) {
        wide_total = scaled(logical_toggle_width + logical_edit_width +
                                logical_button_gap,
                            dpi);
    } else {
        wide_total = scaled(logical_toggle_width, dpi);
    }
    const bool narrow = content_right - row.left - wide_total <
                        scaled(logical_name_reserve, dpi);
    if (tight) *tight = narrow;

    std::vector<std::pair<Button, RECT>> out;
    int cursor = content_right;
    const auto push = [&](Button button, int width_value) {
        const int width = scaled(narrow ? logical_narrow_width : width_value, dpi);
        const int left = cursor - width;
        cursor = left - gap;
        out.push_back({button, RECT{left, top, left + width, bottom}});
    };
    if (kind == Row::Actions::full) {
        // 删除 编辑 开关, left to right: the toggle closes the row at the
        // right edge, matching the Windows settings rhythm.
        push(Button::enable, logical_toggle_width);
        push(Button::edit, logical_edit_width);
        push(Button::remove, logical_remove_width);
    } else if (kind == Row::Actions::toggle_edit) {
        // 编辑 sits left of 启用 on the DeepL row, the reverse of the
        // endpoint rows where 删除/编辑 read right-to-left. The toggle keeps
        // a narrower slot than the text buttons so the pill sits at the same
        // column as the glyphs beside it.
        push(Button::enable, logical_toggle_width);
        push(Button::edit, logical_edit_width);
    } else {
        push(Button::enable, logical_toggle_width);
    }
    std::reverse(out.begin(), out.end());
    return out;
}

Button hit_test(const RECT& row, POINT point, int dpi) {
    for (const auto& [button, bounds] : buttons(row, dpi, nullptr)) {
        if (PtInRect(&bounds, point)) return button;
    }
    return Button::none;
}

Button hit_test(const RECT& row, POINT point, int dpi, const Row* row_state) {
    for (const auto& [button, bounds] : buttons(row, dpi, row_state, nullptr)) {
        if (PtInRect(&bounds, point)) return button;
    }
    return Button::none;
}

std::pair<int, Button> hit_test(HWND list, POINT client_point, int dpi,
                                const std::vector<Row>& rows) {
    const int count =
        list ? static_cast<int>(SendMessageW(list, LB_GETCOUNT, 0, 0)) : 0;
    for (int index = 0; index < count; ++index) {
        RECT bounds{};
        if (SendMessageW(list, LB_GETITEMRECT, static_cast<WPARAM>(index),
                         reinterpret_cast<LPARAM>(&bounds)) == LB_ERR) {
            continue;
        }
        if (PtInRect(&bounds, client_point)) {
            const Row* row_state =
                index >= 0 && static_cast<std::size_t>(index) < rows.size()
                    ? &rows[static_cast<std::size_t>(index)] : nullptr;
            return {index, hit_test(bounds, client_point, dpi, row_state)};
        }
    }
    return {-1, Button::none};
}

void invalidate_row(HWND list, int index) {
    if (!list || index < 0) return;
    RECT bounds{};
    if (SendMessageW(list, LB_GETITEMRECT, static_cast<WPARAM>(index),
                     reinterpret_cast<LPARAM>(&bounds)) != LB_ERR) {
        // The row paints every pixel it owns, so an erase pass would only show a
        // flash of the class background underneath it.
        InvalidateRect(list, &bounds, FALSE);
    }
}

void self_test() {
    constexpr int dpi = 96;
    const RECT wide{0, 0, 480, logical_height};
    bool tight = false;
    const auto actions = buttons(wide, dpi, &tight);
    if (actions.size() != 3 || tight) {
        throw AppError("large-model list self-test lost its action buttons");
    }
    // Row order is 删除 编辑 开关, so the first button is the widest-text one.
    const int expected[3] = {logical_remove_width, logical_edit_width,
                             logical_toggle_width};
    // buttons() hugs the right edge with the same 4px margin the rows use.
    const int content_right = wide.right - 4;
    int previous_right = wide.left;
    for (std::size_t index = 0; index < actions.size(); ++index) {
        const auto& bounds = actions[index].second;
        if (bounds.right <= bounds.left || bounds.bottom <= bounds.top ||
            bounds.left < previous_right || bounds.right > content_right ||
            bounds.bottom > wide.bottom - 4 || bounds.top < wide.top + 4) {
            throw AppError("large-model list self-test action geometry is wrong");
        }
        if (bounds.right - bounds.left != expected[index]) {
            throw AppError("large-model list self-test action width changed");
        }
        previous_right = static_cast<int>(bounds.right) + logical_button_gap;
        const POINT center{(bounds.left + bounds.right) / 2,
                           (bounds.top + bounds.bottom) / 2};
        if (hit_test(wide, center, dpi) != actions[index].first) {
            throw AppError("large-model list self-test hit test missed a button");
        }
    }
    if (hit_test(wide, POINT{20, 20}, dpi) != Button::none) {
        throw AppError("large-model list self-test hit a button over the name");
    }
    // The last button is the toggle now; the pixel just inside its 4px edge
    // margin must hit it, and the gutter to the right stays inert.
    if (hit_test(wide,
                 POINT{wide.right - 5, (wide.top + wide.bottom) / 2},
                 dpi) != Button::enable) {
        throw AppError("large-model list self-test missed the padded right edge");
    }
    if (hit_test(wide, POINT{wide.right - 2, (wide.top + wide.bottom) / 2}, dpi) !=
        Button::none) {
        throw AppError("large-model list self-test hit a button inside the gutter");
    }
    const RECT narrow{0, 0, 190, logical_height};
    bool narrow_tight = false;
    const auto narrow_actions = buttons(narrow, dpi, &narrow_tight);
    if (!narrow_tight || narrow_actions.size() != 3 ||
        narrow_actions[0].second.left <= narrow.left ||
        narrow_actions[2].second.right > narrow.right - 4) {
        throw AppError("large-model list self-test narrow layout does not fit");
    }
    const auto empty_actions = buttons(RECT{0, 0, 0, logical_height}, dpi, nullptr);
    if (!empty_actions.empty()) {
        throw AppError("large-model list self-test drew buttons with no width");
    }

    const std::vector<settings_profiles::Profile> list{
        {L"甲", L"openai_chat", L"https://a.test/v1", L"k", L"m", true},
        {L"乙", L"openai_chat", L"", L"", L"", false},
    };
    const auto rows = make_rows(list);
    if (rows.size() != 2 || !rows[0].enabled || !rows[0].configured ||
        rows[1].enabled || rows[1].configured ||
        enable_label(rows[0]) != L"停用" || enable_label(rows[1]) != L"启用") {
        throw AppError("large-model list self-test row state is wrong");
    }
}

}  // namespace minitrans::settings_ai_list
