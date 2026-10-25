#pragma once

#include "settings_profiles.hpp"

#include <windows.h>

#include <array>
#include <cstdint>
#include <memory>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace minitrans {

// Modal add/edit window for one user-defined large-model endpoint. The page
// lists the endpoints, this window owns the fields: name, protocol, base URL,
// API key and model, plus the model list fetch and the connection test.
class SettingsAiDialog {
public:
    struct Result {
        bool saved{};
        settings_profiles::Profile profile;
    };

    // Shows the dialog modally over `owner` and returns the edited endpoint.
    // `suggested_name` is the free name a new endpoint gets before typing.
    static Result edit(HINSTANCE instance, HWND owner,
                       const settings_profiles::Profile& current,
                       std::wstring_view original_name,
                       std::wstring_view suggested_name);

    static void self_test(HINSTANCE instance, HWND owner);

private:
    struct ModelListResult {
        std::uint64_t generation{};
        std::vector<std::wstring> models;
        std::wstring error;
        bool cancelled{};
    };

    static constexpr UINT models_completed_message = WM_APP + 71;
    static constexpr UINT test_completed_message = WM_APP + 72;
    static constexpr UINT clear_selection_message = WM_APP + 73;

    SettingsAiDialog(HINSTANCE instance, const settings_profiles::Profile& current,
                     std::wstring_view original_name, std::wstring_view suggested_name);
    ~SettingsAiDialog();

    SettingsAiDialog(const SettingsAiDialog&) = delete;
    SettingsAiDialog& operator=(const SettingsAiDialog&) = delete;

    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK edit_proc(HWND control, UINT message, WPARAM wparam,
                                      LPARAM lparam, UINT_PTR subclass_id,
                                      DWORD_PTR reference);
    static LRESULT CALLBACK combo_proc(HWND control, UINT message, WPARAM wparam,
                                       LPARAM lparam, UINT_PTR subclass_id,
                                       DWORD_PTR reference);
    static LRESULT CALLBACK combo_edit_proc(HWND control, UINT message, WPARAM wparam,
                                            LPARAM lparam, UINT_PTR subclass_id,
                                            DWORD_PTR reference);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    void create_window(HWND owner);
    void create_controls();
    void create_theme_resources();
    void layout_controls();
    void paint();
    void draw_combo(HWND control, HDC dc);
    LRESULT draw_item(const DRAWITEMSTRUCT& item);

    // Reads the fields into `profile`; on failure `error` carries the message
    // shown in the footer status and the result stays unsaved. collect()
    // additionally requires the model, collect_endpoint() does not.
    bool collect(settings_profiles::Profile& profile, std::wstring& error) const;
    bool collect_endpoint(settings_profiles::Profile& profile,
                          std::wstring& error) const;
    // The selected entry of ai_protocols, from the segmented protocol row.
    [[nodiscard]] int protocol_segment() const noexcept;
    void set_protocol_segment(int index);
    void set_status(std::wstring_view message, bool error);
    void start_model_refresh();
    void finish_model_refresh(ModelListResult result);
    void cancel_model_refresh() noexcept;
    void discard_model_refresh_results(HWND target) noexcept;
    void start_connection_test();
    void finish_connection_test(bool success, std::wstring message);
    void discard_connection_test_results(HWND target) noexcept;
    void close(bool saved);

    HINSTANCE instance_{};
    settings_profiles::Profile original_;
    std::wstring original_name_;
    HWND window_{};
    HWND name_{};
    // 协议 is one segmented row of three owner-drawn chips, one per entry of
    // settings_internal::ai_protocols.
    std::array<HWND, 3> protocol_segment_{};
    HWND endpoint_{};
    HWND key_{};
    HWND model_{};
    HWND model_refresh_{};
    HWND test_{};
    HWND status_{};
    HWND save_{};
    HFONT font_{};
    HFONT small_font_{};
    HBRUSH background_{};
    HBRUSH card_background_{};
    HBRUSH input_background_{};
    // The frame of every field and the card behind them are painted by this
    // window, so the layout stores the rectangles the paint pass needs.
    struct Placement {
        RECT card{};
        std::array<RECT, 5> labels{};
        std::array<RECT, 5> shells{};
        RECT refresh{};
        RECT test{};
        RECT status{};
        RECT save{};
        RECT cancel{};
    };

    [[nodiscard]] Placement place() const noexcept;
    Placement placement_{};
    std::wstring status_text_;
    std::string window_error_;
    int dpi_{96};
    bool loading_{};
    bool status_error_{};
    bool test_success_{};
    bool test_pending_{};
    bool model_refreshing_{};
    bool closed_{};
    Result result_;
    std::jthread model_thread_;
    std::jthread test_thread_;
    std::uint64_t model_generation_{};
};

}  // namespace minitrans
