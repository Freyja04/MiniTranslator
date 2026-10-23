#pragma once

#include "text_translation_session.hpp"

#include <windows.h>

#include <functional>
#include <string>

namespace minitrans {

class QuickTranslateWindow {
public:
    QuickTranslateWindow(HINSTANCE instance, TextTranslationSession& session);
    ~QuickTranslateWindow();

    QuickTranslateWindow(const QuickTranslateWindow&) = delete;
    QuickTranslateWindow& operator=(const QuickTranslateWindow&) = delete;

    void show(HWND owner);
    void toggle_near(HWND owner, POINT anchor, bool select_all);
    void hide();
    void refresh();
    void refresh_appearance();
    void self_test(HWND owner);
    [[nodiscard]] bool preprocess_message(MSG& message);
    [[nodiscard]] bool visible() const noexcept;
    void set_callbacks(TextTranslationCallbacks callbacks) {
        callbacks_ = std::move(callbacks);
    }

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam);
    static LRESULT CALLBACK input_proc(HWND control, UINT message,
                                       WPARAM wparam, LPARAM lparam,
                                       UINT_PTR subclass_id, DWORD_PTR reference);
    static LRESULT CALLBACK target_combo_proc(HWND control, UINT message,
                                              WPARAM wparam, LPARAM lparam,
                                              UINT_PTR subclass_id,
                                              DWORD_PTR reference);
    LRESULT handle_message(UINT message, WPARAM wparam, LPARAM lparam);
    void create_window(HWND owner);
    void create_controls();
    void create_theme_resources();
    void destroy_theme_resources() noexcept;
    void layout_controls();
    void show_at(HWND owner, POINT anchor, bool select_all);
    void sync_input();
    void sync_source();
    void sync_target();
    void on_input_changed();
    void on_source_changed();
    void on_target_changed();
    void swap_languages();
    LRESULT draw_item(const DRAWITEMSTRUCT& item);
    void draw_target_combo(HDC dc);
    void draw_source_combo(HDC dc);
    void paint();

    HINSTANCE instance_{};
    TextTranslationSession& session_;
    TextTranslationCallbacks callbacks_;
    HWND window_{};
    HWND source_combo_{};
    HWND swap_button_{};
    HWND target_combo_{};
    HWND input_{};
    HWND output_{};
    HWND copy_button_{};
    HWND clear_button_{};
    HWND capture_button_{};
    HWND home_button_{};
    HWND close_button_{};
    HFONT font_{};
    HFONT small_font_{};
    HFONT icon_font_{};
    // Clear/copy sit next to their 原文/译文 captions, so their glyphs stay at
    // caption size instead of the larger toolbar icon size.
    HFONT action_icon_font_{};
    // The close cross needs to read slightly smaller than the capture glyph.
    HFONT close_icon_font_{};
    HBRUSH background_{};
    HBRUSH surface_{};
    HBRUSH input_background_{};
    COLORREF accent_{};
    int dpi_{96};
    bool syncing_{};
    bool status_error_{};
};

}  // namespace minitrans
