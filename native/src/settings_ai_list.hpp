#pragma once

#include "settings_profiles.hpp"

#include <windows.h>

#include <string>
#include <utility>
#include <vector>

namespace minitrans::settings_ai_list {

// The large-model endpoint list is an owner-drawn list box: the rows keep the
// card style and the page never grows three child controls per endpoint. The
// page, its painting and its hit testing all read the geometry here, so a click
// can never land on another button than the one the user saw.
struct Row {
    std::wstring name;
    bool enabled{};
    bool configured{};
    // Which action buttons the row carries: the key-free traditional
    // providers only toggle, DeepL also edits its key, and the endpoint rows
    // carry the full toggle/edit/delete set.
    enum class Actions { full, toggle, toggle_edit };
    Actions actions{Actions::full};
};

[[nodiscard]] std::vector<Row> make_rows(
    const std::vector<settings_profiles::Profile>& list);

enum class Button { none, enable, edit, remove };

// One row per endpoint: the item height, the painted separator and the card
// bottom all come from this single logical number.
[[nodiscard]] int logical_row_height() noexcept;

[[nodiscard]] int logical_row_padding() noexcept;

// The enable button reads 停用 when the row is already the active endpoint, so
// large-model translation can be switched off without picking another row.
[[nodiscard]] std::wstring enable_label(const Row& row) noexcept;

// Buttons for one row under its Actions kind, right-aligned in the row
// rectangle; `tight` reports glyph mode. The enable entry is always first in
// the returned order used by hit testing and painting.
[[nodiscard]] std::vector<std::pair<Button, RECT>> buttons(const RECT& row,
                                                           int dpi,
                                                           const Row* row_state,
                                                           bool* tight);

[[nodiscard]] int scaled(int logical, int dpi) noexcept;

// Action buttons for one row, right aligned inside the row rectangle. `tight`
// reports that the row is too short for the text captions and the caller should
// draw glyphs instead.
[[nodiscard]] std::vector<std::pair<Button, RECT>> buttons(const RECT& row, int dpi,
                                                           bool* tight);

[[nodiscard]] Button hit_test(const RECT& row, POINT point, int dpi);

// Same, with the row's action kind deciding which buttons exist.
[[nodiscard]] Button hit_test(const RECT& row, POINT point, int dpi,
                              const Row* row_state);

// Row index and button under a point in the list box client area; the row is -1
// and the button none when the point misses every row. The rows carry the
// per-row action kind.
[[nodiscard]] std::pair<int, Button> hit_test(HWND list, POINT client_point,
                                              int dpi,
                                              const std::vector<Row>& rows);

// Repaint a single row. Rows are painted inside the list box, so hovering and
// the two-step delete only dirty the row they change.
void invalidate_row(HWND list, int index);

void self_test();

}  // namespace minitrans::settings_ai_list
