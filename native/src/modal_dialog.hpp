#pragma once

#include <windows.h>

#include <string>

namespace minitrans {

// Shared skeleton for the app's borderless modal popups: class registration,
// DPI-aware centred creation, the modal message loop, the close path and the
// error-safe window procedure. A subclass owns its controls, layout, painting
// and the messages specific to it.
class ModalDialog {
public:
    virtual ~ModalDialog();

    ModalDialog(const ModalDialog&) = delete;
    ModalDialog& operator=(const ModalDialog&) = delete;

protected:
    ModalDialog(HINSTANCE instance, HWND owner);

    // Registers `class_name` and creates the popup centred over the owner.
    void create_popup(const wchar_t* class_name, const wchar_t* title,
                      int logical_width, int logical_height);
    // Shows the window and pumps messages until close() runs.
    void run_modal();
    // The font and the three brushes every dialog paints with. A dialog that
    // needs an extra font overrides this and calls the base first.
    virtual void create_theme_resources();
    void destroy_theme_resources() noexcept;

    // Creates and lays out the controls; called once the window exists.
    virtual void create_controls() = 0;
    // Everything the base does not handle itself.
    virtual LRESULT handle_dialog_message(UINT message, WPARAM wparam,
                                          LPARAM lparam) = 0;
    // Destroys the window and records the outcome; sets closed_.
    virtual void close(bool saved) = 0;
    // True when the modal loop should swallow Escape instead of letting
    // IsDialogMessageW turn it into IDCANCEL.
    [[nodiscard]] virtual bool closes_on_escape() const noexcept { return false; }
    // The control that takes focus once the dialog is visible.
    [[nodiscard]] virtual HWND initial_focus() const noexcept { return window_; }

    HINSTANCE instance_{};
    HWND owner_{};
    HWND window_{};
    int dpi_{96};
    bool closed_{};
    HFONT font_{};
    HBRUSH background_{};
    HBRUSH card_background_{};
    HBRUSH input_background_{};
    std::string window_error_;

private:
    static LRESULT CALLBACK window_proc(HWND window, UINT message,
                                        WPARAM wparam, LPARAM lparam);
    LRESULT dispatch(UINT message, WPARAM wparam, LPARAM lparam);
};

}  // namespace minitrans
