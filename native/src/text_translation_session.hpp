#pragma once

#include "config.hpp"
#include "language_catalog.hpp"
#include "pipeline.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace minitrans {

enum class TextTranslationState {
    idle,
    waiting,
    translating,
    ready,
    error,
    too_long,
};

struct TextTranslationCallbacks {
    std::function<void(std::wstring)> input_changed;
    std::function<void(std::wstring)> source_changed;
    std::function<void(std::wstring)> target_changed;
    std::function<void(bool)> composition_changed;
    std::function<void()> translate_now;
    std::function<void()> clear;
    std::function<void()> copy;
    std::function<void()> copy_source;
    std::function<void()> clear_output;
    std::function<void()> capture;
    std::function<void()> open_settings;
    // Opens the main window, mirroring the tray menu's 首页 entry.
    std::function<void()> home;
    // The quick window can hide itself (Esc, its close button, or the toggle
    // hotkey), so the host needs a signal to cancel translation work that is
    // still in flight and can no longer be shown.
    std::function<void()> hidden;
};

class TextTranslationSession {
public:
    static constexpr std::size_t max_characters = 500;

    TextTranslationSession(ConfigStore& config, PipelineController& pipeline);

    bool set_input(std::wstring value);
    bool set_source(std::wstring value);
    bool set_target(std::wstring value);
    void clear();
    void clear_output();
    bool submit();
    bool configuration_changed();
    bool handle_completion(PipelineCompletion completion);
    void cancel();
    // Resolve the provider and the language pair while the window is opening so
    // the first submit does not pay for reading the configuration again.
    void prewarm();
    void self_test();

    // The two translation surfaces show a short confirmation in their footer
    // status line after the user copies the translation, then fall back to the
    // regular status text.
    static constexpr auto copy_confirmation_duration = std::chrono::seconds(2);
    void note_copied();
    void clear_copy_confirmation();
    [[nodiscard]] bool copy_confirmation_visible() const noexcept;

    void set_changed_callback(std::function<void()> callback) {
        changed_callback_ = std::move(callback);
    }

    [[nodiscard]] const std::wstring& input() const noexcept { return input_; }
    [[nodiscard]] const std::wstring& output() const noexcept { return output_; }
    [[nodiscard]] const std::wstring& error() const noexcept { return error_; }
    [[nodiscard]] const std::wstring& source() const noexcept { return source_; }
    [[nodiscard]] const std::wstring& target() const noexcept { return target_; }
    [[nodiscard]] TextTranslationState state() const noexcept { return state_; }
    [[nodiscard]] std::size_t character_count() const noexcept;
    [[nodiscard]] std::wstring effective_source() const;
    [[nodiscard]] std::wstring effective_target() const;
    [[nodiscard]] bool can_submit() const noexcept;
    [[nodiscard]] std::uint64_t request_id() const noexcept { return request_id_; }

private:
    [[nodiscard]] bool has_visible_text() const noexcept;
    [[nodiscard]] std::wstring configuration_signature() const;
    // Re-resolving the provider and the language pair re-reads the whole
    // configuration JSON, so keep the last resolution until settings change.
    [[nodiscard]] const PipelineOptions& current_options();
    void invalidate(TextTranslationState next_state);
    void notify_changed();

    ConfigStore& config_;
    PipelineController& pipeline_;
    std::optional<PipelineOptions> options_cache_;
    std::wstring input_;
    std::wstring output_;
    std::wstring error_;
    std::wstring source_;
    std::wstring target_;
    std::wstring effective_target_;
    std::wstring submitted_configuration_;
    TextTranslationState state_{TextTranslationState::idle};
    std::uint64_t request_id_{};
    std::chrono::steady_clock::time_point copy_confirmation_until_{};
    std::function<void()> changed_callback_;
};

const std::array<std::wstring_view, language_catalog.size()>& text_translation_targets() noexcept;

}  // namespace minitrans
