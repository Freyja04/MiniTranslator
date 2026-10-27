#pragma once

#include "config.hpp"
#include "pipeline.hpp"
#include "result_window.hpp"
#include "selection_overlay.hpp"
#include "self_test_layers.hpp"
#include "settings_window.hpp"
#include "text_translation_window.hpp"
#include "updater.hpp"

#include <windows.h>
#include <shellapi.h>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace minitrans {

class BlockEditor;

class AppHost {
public:
    // self_test_layers only matters when the host is later run in self-test
    // mode: layers that are not selected skip the windows they own.
    explicit AppHost(HINSTANCE instance,
                     std::filesystem::path config_path = {},
                     SelfTestLayer self_test_layers = SelfTestLayer::all);
    ~AppHost();

    AppHost(const AppHost&) = delete;
    AppHost& operator=(const AppHost&) = delete;

    // self_test_layers narrows --self-test to the named layers; the default
    // keeps the full sweep CI runs.
    int run(bool self_test = false, bool update_self_test = false,
            bool text_translation_on_start = false,
            SelfTestLayer self_test_layers = SelfTestLayer::all);
    [[nodiscard]] HWND handle() const noexcept { return window_; }
    static constexpr const wchar_t* window_class_name() noexcept {
        return L"MiniTranslator.Native.MessageWindow.v1";
    }
    static constexpr UINT activate_message = WM_APP + 1;
    static constexpr WPARAM activate_text_translation = 1;
    // Inline update flow: the worker reports download progress and the final
    // outcome back to the message window so the settings page can stay open.
    static constexpr UINT settings_update_stage_message = WM_APP + 43;
    static constexpr UINT settings_update_completed_message = WM_APP + 42;
    // Posted by the background silent check that runs outside the settings page.
    static constexpr UINT silent_update_completed_message = WM_APP + 41;
    enum class SettingsUpdateOutcome : WPARAM {
        no_update = 0,
        updated = 1,
        failed = 2,
        cancelled = 3,
    };

private:
    // Identifiers the message handlers in app_messages.cpp share with the
    // constructor and the tray helpers in app.cpp.
    static constexpr UINT tray_message = WM_APP + 2;
    static constexpr UINT initialize_message = WM_APP + 3;
    static constexpr UINT_PTR capture_timer = 1;
    static constexpr UINT_PTR recapture_timer = 2;
    static constexpr UINT_PTR update_timer = 3;
    static constexpr UINT_PTR text_translate_timer = 4;
    static constexpr UINT_PTR copy_toast_timer = 5;
    // Long enough that the tray icon, hotkeys and the first user interaction
    // all happen before a network check starts competing for the same thread
    // pool.
    static constexpr UINT update_check_delay_ms = 4000;
    static constexpr int hotkey_capture = 1;
    static constexpr int hotkey_toggle = 2;
    static constexpr int hotkey_text_translate = 3;
    static constexpr UINT command_capture = 3001;
    static constexpr UINT command_restore = 3002;
    static constexpr UINT command_settings = 3003;
    static constexpr UINT command_exit = 3006;
    static constexpr UINT command_text_translate = 3007;
    static constexpr UINT tray_icon_id = 1;
    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    void create_message_window();
    void refresh_theme_windows();
    void add_tray_icon();
    void remove_tray_icon();
    void refresh_tray_icon();
    void refresh_tray_tooltip();
    void show_tray_menu(POINT location);
    void notify(std::wstring_view title, std::wstring_view message);
    bool apply_hotkeys(bool announce_changes);
    void start_capture();
    void start_capture_from_settings();
    void begin_capture_now();
    void on_selected(const RECT& rect, PixelBuffer image);
    void start_pipeline(std::shared_ptr<const PixelBuffer> image, const RECT& rect);
    // Resolving the provider and the language pair is cheap but not free: the
    // configuration JSON is re-read and the endpoint is re-selected every time.
    // Warm it once at startup and after settings changes so the first capture
    // does not pay for it.
    void refresh_pipeline_options();
    const PipelineOptions& pipeline_options();
    void process_pipeline_completions();
    void open_text_translation();
    void toggle_quick_translation();
    void on_text_input_changed(std::wstring value);
    void on_text_source_changed(std::wstring value);
    void on_text_target_changed(std::wstring value);
    void on_text_composition_changed(bool composing);
    void schedule_text_translation(bool immediate = false);
    void refresh_text_translation_views();
    void toggle_result();
    void copy_to_clipboard(std::wstring_view text);
    void retry_translation();
    void open_block_editor();
    void request_block_translation(BlockEditor& editor, std::size_t index,
                                   TextBlock block);
    void cancel_block_translation(BlockEditor& editor) noexcept;
    bool schedule_block_translations(std::vector<std::size_t> indices,
                                     std::vector<TextBlock> blocks,
                                     BlockEditor* editor);
    void process_block_translation(PipelineCompletion completion);
    void refresh_cached_result();
    void request_recapture(const RECT& rect);
    void recapture_now();
    void open_settings();
    void open_translation_settings();
    void start_update_check();
    void handle_settings_update_completed(WPARAM outcome);
    void start_silent_update_check();
    std::optional<UpdateInfo> process_silent_update_check();
    void handle_update_available(const UpdateInfo& update);
    void quit();
    ResultAppearance result_appearance() const;

    HINSTANCE instance_{};
    std::uint64_t applied_visual_revision_{};
    HWND window_{};
    UINT taskbar_created_message_{};
    NOTIFYICONDATAW tray_{};
    HICON icon_{};
    ConfigStore config_;
    SettingsWindow settings_;
    SelectionOverlay overlay_;
    ResultWindow result_;
    std::unique_ptr<PipelineController> pipeline_;
    std::unique_ptr<TextTranslationSession> text_session_;
    std::unique_ptr<QuickTranslateWindow> quick_window_;
    struct BlockTranslationRequest {
        std::uint64_t request_id{};
        std::vector<std::size_t> indices;
        BlockEditor* editor{};
    };
    std::uint64_t current_request_{};
    std::optional<BlockTranslationRequest> block_translation_request_;
    PipelineOptions pipeline_options_{};
    bool pipeline_options_ready_{};
    std::shared_ptr<const PixelBuffer> last_image_;
    std::vector<TextBlock> last_blocks_;
    std::optional<PipelineResult> last_result_;
    std::optional<RECT> pending_recapture_;
    std::jthread silent_update_thread_;
    std::mutex silent_update_mutex_;
    std::optional<UpdateInfo> silent_update_result_;
    std::jthread settings_update_thread_;
    std::wstring registered_capture_hotkey_;
    std::wstring registered_toggle_hotkey_;
    std::wstring registered_text_hotkey_;
    bool text_composing_{};
    bool settings_hidden_for_capture_{};
    bool shutting_down_{};
    bool self_test_mode_{};
    bool update_self_test_mode_{};
    bool text_translation_on_start_{};
};

}  // namespace minitrans
