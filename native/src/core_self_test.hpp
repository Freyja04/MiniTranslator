#pragma once

namespace minitrans {

// Headless checks for window-independent logic: language tables, settings
// constants and hotkey parsing. Runs from --self-test before the app creates
// any window, so it also works where no interactive desktop is available.
// GUI-only assertions (layout, drawing, control state) stay in the per-window
// self-tests.
void core_self_test();

}  // namespace minitrans
