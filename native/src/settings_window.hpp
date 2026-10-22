#pragma once

#include "config.hpp"
#include "settings_ocr.hpp"
#include "settings_profiles.hpp"
#include "text_translation_session.hpp"

#include <windows.h>

#include <array>
#include <cstdint>
#include <functional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace minitrans {

class SettingsWindow {
public:
    SettingsWindow(HINSTANCE instance, ConfigStore& config);
    ~SettingsWindow();

    SettingsWindow(const SettingsWindow&) = delete;
    SettingsWindow& operator=(const SettingsWindow&) = delete;

    void show(HWND owner);
    void show_translation(HWND owner);
    void show_text_translation(HWND owner);
    void refresh_appearance();
    void attach_text_translation(TextTranslationSession& session,
                                 TextTranslationCallbacks callbacks);
    void detach_text_translation() noexcept;
    void refresh_text_translation();
    void self_test(HWND owner);
    [[nodiscard]] bool preprocess_message(MSG& message);
    void set_hotkeys_changed_callback(std::function<bool()> callback) {
        hotkeys_changed_callback_ = std::move(callback);
    }
    void set_restart_callback(std::function<void()> callback) {
        restart_callback_ = std::move(callback);
    }
    void set_settings_changed_callback(std::function<void()> callback) {
        settings_changed_callback_ = std::move(callback);
    }
    void set_update_check_callback(std::function<void()> callback) {
        update_check_callback_ = std::move(callback);
    }
    void set_capture_callback(std::function<void()> callback) {
        capture_callback_ = std::move(callback);
    }
    // Inline update status shown next to the 检查更新 button; the update flow
    // itself lives in AppHost so no dialog window is needed.
    void set_update_status(std::wstring_view text, bool error = false);
    void set_update_check_enabled(bool enabled);
    // Region capture replaces the whole screen with a frozen image, so the
    // settings window steps aside while the user draws the selection.
    void hide_for_capture();
    void show_after_capture();

private:
    struct ModelListResult {
        std::uint64_t generation{};
        std::wstring provider;
        std::wstring profile;
        std::vector<std::wstring> models;
        std::wstring error;
        bool cancelled{};
    };

    // One user-defined large-model endpoint; the naming/selection rules that
    // keep the combo consistent live in settings_profiles.cpp.
    using AiProfile = settings_profiles::Profile;

    static constexpr UINT test_completed_message = WM_APP + 61;
    static constexpr UINT models_completed_message = WM_APP + 62;
    static constexpr UINT clear_combo_selection_message = WM_APP + 63;

    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK combo_proc(HWND control, UINT message,
                                       WPARAM wparam, LPARAM lparam,
                                       UINT_PTR subclass_id, DWORD_PTR reference);
    static LRESULT CALLBACK combo_edit_proc(HWND control, UINT message,
                                            WPARAM wparam, LPARAM lparam,
                                            UINT_PTR subclass_id, DWORD_PTR reference);
    static LRESULT CALLBACK edit_proc(HWND control, UINT message,
                                      WPARAM wparam, LPARAM lparam,
                                      UINT_PTR subclass_id, DWORD_PTR reference);
    static LRESULT CALLBACK hotkey_proc(HWND control, UINT message,
                                        WPARAM wparam, LPARAM lparam,
                                        UINT_PTR subclass_id, DWORD_PTR reference);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    void create_window(HWND owner);
    void create_controls();
    void create_hotkey_page();
    void create_translation_page();
    void create_ocr_page();
    void create_other_page();
    void create_text_translation_page();
    void create_theme_resources();
    void apply_control_theme(HWND control);
    void layout_controls();
    void show_page(int index);
    void paint();
    void draw_combo(HWND control, HDC dc);
    LRESULT draw_item(const DRAWITEMSTRUCT& item);
    bool is_checkbox(HWND control) const noexcept;
    bool checkbox_checked(HWND control) const noexcept;
    void set_checkbox(HWND control, bool checked);
    HWND add_control(int page, DWORD ex_style, const wchar_t* type,
                     const wchar_t* text, DWORD style, int identifier = 0);
    HWND add_label(int page, const wchar_t* text);
    void load_values();
    bool save_values();
    void save_provider_fields();
    void refresh_provider_fields(bool load);
    void save_profile_fields();
    void populate_profile_names();
    void select_profile(std::wstring_view name);
    void add_profile();
    void remove_profile();
    [[nodiscard]] std::vector<AiProfile> read_profiles() const;
    void write_profiles(const std::vector<AiProfile>& list);
    [[nodiscard]] int profile_index() const noexcept;
    [[nodiscard]] std::wstring current_options_json() const;
    void start_connection_test();
    void finish_connection_test(bool success, std::wstring message);
    void discard_connection_test_results(HWND target = nullptr) noexcept;
    void start_model_refresh();
    void finish_model_refresh(ModelListResult result);
    void cancel_model_refresh(bool clear_status = false) noexcept;
    void discard_model_refresh_results(HWND target = nullptr) noexcept;
    void set_status(std::wstring_view message, bool error = false);
    void close();

    HINSTANCE instance_{};
    ConfigStore& config_;
    TextTranslationSession* text_session_{};
    TextTranslationCallbacks text_callbacks_;
    HWND window_{};
    std::array<HWND, 5> navigation_{};
    HWND status_{};
    HWND capture_hotkey_{};
    HWND toggle_hotkey_{};
    HWND text_hotkey_{};
    HWND translator_class_{};
    HWND provider_label_{};
    HWND provider_{};
    HWND profile_label_{};
    HWND profile_select_{};
    HWND profile_add_{};
    HWND profile_remove_{};
    HWND protocol_label_{};
    HWND ai_protocol_{};
    HWND provider_key_label_{};
    HWND provider_key_{};
    HWND provider_endpoint_label_{};
    HWND provider_endpoint_{};
    HWND provider_model_label_{};
    HWND provider_model_{};
    HWND model_refresh_{};
    HWND test_button_{};
    HWND test_status_{};
    HWND provider_hint_{};
    HWND ocr_engine_value_{};
    HWND ocr_languages_{};
    HWND translation_target_{};
    HWND ocr_upscale_{};
    HWND theme_mode_{};
    HWND auto_copy_{};
    HWND autostart_{};
    HWND restart_{};
    HWND check_updates_{};
    HWND update_status_{};
    HWND text_capture_{};
    HWND text_source_{};
    HWND text_swap_{};
    HWND text_target_{};
    HWND text_input_{};
    HWND text_output_{};
    HWND text_clear_{};
    HWND text_copy_{};
    HFONT font_{};
    HFONT small_font_{};
    HFONT icon_font_{};
    // The 原文/译文 clear and copy glyphs sit beside their captions and match
    // the caption size instead of the larger navigation icon size.
    HFONT action_icon_font_{};
    HBRUSH background_{};
    HBRUSH sidebar_background_{};
    HBRUSH card_background_{};
    HBRUSH input_background_{};
    std::array<std::vector<HWND>, 5> page_controls_;
    std::vector<settings_ocr::Row> ocr_language_options_;
    std::string window_error_;
    std::wstring loaded_provider_;
    std::wstring loaded_profile_;
    int field_offset_{};
    int dpi_{96};
    int current_page_{};
    bool finished_{};
    bool loading_{};
    bool status_error_{};
    bool test_success_{};
    bool test_pending_{};
    bool model_refreshing_{};
    bool syncing_text_{};
    bool update_status_error_{};
    std::jthread test_thread_;
    std::jthread model_thread_;
    std::uint64_t model_generation_{};
    std::function<bool()> hotkeys_changed_callback_;
    std::function<void()> restart_callback_;
    std::function<void()> settings_changed_callback_;
    std::function<void()> update_check_callback_;
    std::function<void()> capture_callback_;
};

}  // namespace minitrans
