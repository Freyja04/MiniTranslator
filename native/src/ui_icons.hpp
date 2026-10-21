#pragma once

namespace minitrans::ui_icons {

// Semantic action icons shared by the tray menu, quick translation panel, and
// settings navigation. These are deliberately separate from the brand icon
// (IDI_APP_ICON), which belongs to the application identity only.
inline constexpr wchar_t text_translation = static_cast<wchar_t>(0xE8D2);
inline constexpr wchar_t capture_translation = static_cast<wchar_t>(0xE7A8);

}  // namespace minitrans::ui_icons
