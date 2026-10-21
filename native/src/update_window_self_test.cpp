#include "update_window_internal.hpp"

#include "util.hpp"

#include <commctrl.h>

namespace minitrans {

using namespace update_internal;

void UpdateWindow::self_test(HWND owner) {
    if (window_) throw AppError("update self-test window already exists");
    view_state_ = ViewState::idle;
    state_title_ = L"准备检查更新";
    state_message_ = L"将从 GitHub Release 获取稳定版发布信息。";
    details_visible_ = false;
    progress_visible_ = false;
    create_window(owner);
    if (!window_ || !check_button_ || !release_button_ || !action_button_ ||
        !state_message_control_ || !published_value_control_ ||
        !artifact_value_control_ || !source_control_ || !repository_tooltip_ ||
        !font_ || !title_font_ || !background_) {
        throw AppError("update self-test did not create required controls");
    }
    for (const HWND control : {state_message_control_, published_value_control_,
                               artifact_value_control_, source_control_}) {
        if ((GetWindowLongPtrW(control, GWL_STYLE) & WS_TABSTOP) != 0) {
            throw AppError("selectable update text entered the keyboard tab order");
        }
    }
    SendMessageW(state_message_control_, EM_SETSEL, 0, 2);
    DWORD selection_start = 0;
    DWORD selection_end = 0;
    SendMessageW(state_message_control_, EM_GETSEL,
                 reinterpret_cast<WPARAM>(&selection_start),
                 reinterpret_cast<LPARAM>(&selection_end));
    if (selection_start != 0 || selection_end != 2 ||
        SendMessageW(repository_tooltip_, TTM_GETTOOLCOUNT, 0, 0) != 1) {
        throw AppError("update selectable text or repository tooltip self-test failed");
    }
    DestroyWindow(window_);
    window_ = nullptr;
}

}  // namespace minitrans
