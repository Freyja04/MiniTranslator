#pragma once

#include <windows.h>

#include <stop_token>
#include <string>
#include <string_view>
#include <thread>

namespace minitrans {

// Modal one-field window that edits a provider's API key. It exists because
// the traditional provider list only needs a key for DeepL; a full endpoint
// dialog would ask five questions about a single secret. It also carries a
// 测试连接 button so the key can be verified before it is kept.
class ProviderKeyDialog {
public:
    struct Result {
        bool saved{};
        std::wstring key;
    };

    // Shows the window modally over `owner`; `title` names the provider, and
    // `current_key` seeds the field.
    static Result edit(HINSTANCE instance, HWND owner,
                       std::wstring_view title, std::wstring_view current_key);

private:
    ProviderKeyDialog(HINSTANCE instance, HWND owner, std::wstring_view title,
                      std::wstring_view current_key);
    ~ProviderKeyDialog();

    ProviderKeyDialog(const ProviderKeyDialog&) = delete;
    ProviderKeyDialog& operator=(const ProviderKeyDialog&) = delete;

    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    void create_window();
    void create_controls();
    void layout();
    void start_connection_test();
    void finish_connection_test(bool success, std::wstring message);
    void close(bool saved);

    HINSTANCE instance_{};
    HWND owner_{};
    std::wstring title_;
    std::wstring key_;
    HWND window_{};
    HWND field_{};
    HWND test_{};
    HWND save_{};
    HWND cancel_{};
    HFONT font_{};
    HBRUSH background_{};
    HBRUSH card_background_{};
    HBRUSH input_background_{};
    // The card and field rectangles the paint pass draws against.
    RECT card_{};
    RECT title_rect_{};
    RECT label_rect_{};
    RECT field_rect_{};
    RECT test_rect_{};
    RECT save_rect_{};
    RECT cancel_rect_{};
    // The status line under the field shows 测试连接's outcome.
    std::wstring status_text_;
    RECT status_rect_{};
    bool status_error_{};
    bool test_pending_{};
    std::jthread test_thread_;
    // Posted by the worker when the connection attempt completes.
    static constexpr UINT test_completed_message = WM_APP + 75;
    int dpi_{96};
    bool closed_{};
    Result result_;
    std::string window_error_;
};

}  // namespace minitrans
