#pragma once

#include "config.hpp"
#include "settings_ocr.hpp"
#include "settings_ai_list.hpp"
#include "settings_profiles.hpp"
#include "text_translation_session.hpp"

#include <windows.h>

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <stop_token>
#include <string>
#include <utility>
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
    // One user-defined large-model endpoint; the naming/selection rules that
    // keep the combo consistent live in settings_profiles.cpp.
    using AiProfile = settings_profiles::Profile;

    // Page 1 places the class switch, the provider combo branch and the endpoint
    // list branch. The layout pass computes the card once and the paint pass reads
    // it back, so the card and its separators can never disagree with the controls
    // they frame. Everything here is in physical pixels: the paint pass must not
    // scale any of it a second time. Endpoint rows are painted by the list box from
    // its own item rectangles, so their geometry never has to be mirrored here.
    struct TranslationGeometry {
        int card_top{};
        int card_bottom{};
        std::array<int, 3> separators{};
        int separator_count{};
        bool large_model{};
    };

    static constexpr UINT test_completed_message = WM_APP + 61;
    // A second click within this window repeats the delete request; the timer
    // only clears the 「确认?」 caption.
    static constexpr UINT_PTR delete_confirm_timer = 20;
    static constexpr UINT delete_confirm_timeout_ms = 3000;
    // Endpoint list writes land on disk and fan out to the app through this
    // debounce, so a click repaints immediately and never waits on the save.
    static constexpr UINT_PTR profiles_save_timer = 21;
    static constexpr UINT profiles_save_debounce_ms = 150;
    // Every settings write lands on disk through this debounce; the flushes
    // at close/restart/update bypass it.
    static constexpr UINT_PTR config_save_timer = 22;
    static constexpr UINT config_save_debounce_ms = 150;

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
    static LRESULT CALLBACK list_proc(HWND control, UINT message,
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
    void layout_translation_page(int inner_left, int inner_right, int field_left,
                                 int field_width, int control_height);
    [[nodiscard]] int ai_list_item_count() const noexcept;
    void draw_ai_row(HDC dc, const RECT& item, const settings_ai_list::Row& row,
                     bool hovered, settings_ai_list::Button hover_button,
                     bool confirming, bool last);
    void draw_combo(HWND control, HDC dc);
    LRESULT draw_item(const DRAWITEMSTRUCT& item);
    bool is_checkbox(HWND control) const noexcept;
    bool checkbox_checked(HWND control) const noexcept;
    void set_checkbox(HWND control, bool checked);
    // The segmented 主题 row: selection lives in window props on the chips,
    // mirroring how the checkboxes keep their state.
    int theme_selection() const noexcept;
    void set_theme_selection(int index);
    HWND add_control(int page, DWORD ex_style, const wchar_t* type,
                     const wchar_t* text, DWORD style, int identifier = 0);
    HWND add_label(int page, const wchar_t* text);
    void load_values();
    bool save_values();
    void save_provider_fields();
    void refresh_provider_fields();
    // The endpoint list on the 大模型翻译 branch: rows, the enable switch, the
    // add/edit dialog and the two-step delete all live here.
    void populate_ai_list();
    // The traditional provider rows for the shared list control, and the
    // toggle that enables one provider (or DeepL key editing).
    void populate_traditional_list();
    void select_traditional(std::size_t index);
    // Queues the debounced disk save + settings fan-out after a list write;
    // flush_profiles_save() runs it on the timer or before the window closes.
    void schedule_profiles_save();
    void flush_profiles_save();
    // The single disk-write debounce for all settings; flushes immediately
    // when callers must not wait (close, restart, update install).
    void flush_config_save();
    // The shared tail of every endpoint-list write: repopulate, queue the
    // save, re-layout, and repaint the parent-drawn card.
    void finish_ai_list_change();
    // The rows the shared list control is showing right now, so painting and
    // hit testing agree on which page branch is on screen.
    const std::vector<settings_ai_list::Row>& shown_rows() const noexcept;
    void select_ai_profile(std::size_t index);
    void edit_ai_profile(std::optional<std::size_t> index);
    void remove_ai_profile(std::size_t index);
    void reset_delete_confirmation() noexcept;
    [[nodiscard]] std::vector<AiProfile> read_profiles() const;
    void write_profiles(const std::vector<AiProfile>& list);
    [[nodiscard]] std::wstring current_options_json() const;
    [[nodiscard]] bool large_model() const noexcept { return loaded_provider_ == L"ai"; }
    void start_connection_test();
    void finish_connection_test(bool success, std::wstring message);
    void discard_connection_test_results(HWND target = nullptr) noexcept;
    // The endpoint list works in its own client coordinates: the list box
    // subclass forwards the raw point so the row and button under the cursor
    // come from the same geometry the rows were painted with.
    void on_ai_list_click(POINT list_point);
    void set_translation_class(bool large_model);
    // Cursor state for the endpoint rows: the row and the button inside it, so
    // only the painted row under the pointer brightens.
    void update_ai_hover(HWND list, POINT list_point);
    void clear_ai_hover(HWND list) noexcept;
    void set_status(std::wstring_view message, bool error = false);
    void close();
    // Exports the diagnostics report and opens its folder in Explorer.
    void open_diagnostics_folder();

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
    HWND class_traditional_{};
    HWND class_ai_{};
    HWND provider_label_{};
    HWND provider_{};
    HWND provider_key_label_{};
    HWND provider_key_{};
    HWND ai_list_caption_{};
    HWND ai_list_{};
    HWND ai_add_{};
    HWND test_button_{};
    HWND test_status_{};
    HWND provider_hint_{};
    HWND ocr_engine_value_{};
    HWND ocr_languages_{};
    HWND translation_target_{};
    HWND ocr_upscale_{};
    HWND theme_mode_{};
    // 主题 is one segmented row: theme_mode_ is the first chip (跟随系统) and
    // these two carry 浅色 / 深色.
    std::array<HWND, 2> theme_segment_{};
    HWND auto_copy_{};
    HWND autostart_{};
    HWND diagnostics_{};
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
    std::vector<settings_ai_list::Row> ai_rows_;
    // The traditional provider list shares the ai_list_ control; these rows
    // describe the key-free providers plus DeepL in list form.
    std::vector<settings_ai_list::Row> traditional_rows_;
    // The device-pixel row height the list box was last told about. Only a
    // change here needs LB_SETITEMHEIGHT, which repaints the whole list.
    int ai_row_height_{};
    // The row states the list is currently showing, so a toggle only
    // invalidates the rows whose dot or caption actually changed.
    std::vector<settings_ai_list::Row> ai_rows_shown_;
    bool profiles_save_pending_{};
    bool config_save_pending_{};
    // Set when a list write changed the in-memory config, so the debounced
    // save knows the root differs from what the last snapshot recorded.
    bool profile_write_dirty_{};
    std::string window_error_;
    std::wstring loaded_provider_;
    std::wstring pending_delete_;
    TranslationGeometry translation_;
    settings_ai_list::Button ai_hover_button_{settings_ai_list::Button::none};
    bool ai_list_tracking_{};
    DWORD_PTR delete_deadline_{};
    int ai_list_hover_ = -1;
    int field_offset_{};
    int dpi_{96};
    int current_page_{};
    bool finished_{};
    bool loading_{};
    bool status_error_{};
    bool test_success_{};
    bool test_pending_{};
    bool syncing_text_{};
    bool update_status_error_{};
    std::jthread test_thread_;
    std::function<bool()> hotkeys_changed_callback_;
    std::function<void()> settings_changed_callback_;
    std::function<void()> update_check_callback_;
    std::function<void()> capture_callback_;
};

}  // namespace minitrans
