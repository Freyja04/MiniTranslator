#include "settings_window_internal.hpp"

namespace minitrans::settings_internal {

void align_combo_edit(HWND combo, int dpi) noexcept {
    if (!combo) return;
    COMBOBOXINFO info{sizeof(info)};
    if (!GetComboBoxInfo(combo, &info) || !info.hwndItem ||
        info.hwndItem == combo) {
        return;
    }

    RECT item = info.rcItem;
    RECT combo_screen{};
    GetWindowRect(combo, &combo_screen);
    const bool screen_coordinates =
        item.left >= combo_screen.left - 2 && item.top >= combo_screen.top - 2 &&
        item.right <= combo_screen.right + 2 && item.bottom <= combo_screen.bottom + 2;
    if (screen_coordinates) {
        MapWindowPoints(HWND_DESKTOP, combo,
                        reinterpret_cast<POINT*>(&item), 2);
    }

    const int width = item.right - item.left;
    const int height = item.bottom - item.top;
    const int offset = std::min(std::max(1, px(logical_combo_edit_offset_y, dpi)),
                                std::max(0, height - 1));
    if (width <= 0 || height <= offset) return;

    RECT current{};
    if (GetWindowRect(info.hwndItem, &current)) {
        MapWindowPoints(HWND_DESKTOP, combo,
                        reinterpret_cast<POINT*>(&current), 2);
        if (current.left == item.left && current.top == item.top + offset &&
            current.right == item.right && current.bottom == item.bottom) {
            return;
        }
    }
    SetWindowPos(info.hwndItem, nullptr, item.left, item.top + offset,
                 width, height - offset,
                 SWP_NOACTIVATE | SWP_NOZORDER | SWP_NOOWNERZORDER);
}

void set_rounded_control_region(HWND control, int radius) noexcept {
    if (!control) return;
    RECT rect{};
    if (!GetClientRect(control, &rect) || rect.right <= rect.left ||
        rect.bottom <= rect.top) {
        return;
    }
    const int width = rect.right - rect.left;
    const int height = rect.bottom - rect.top;
    const int previous_width = static_cast<int>(
        reinterpret_cast<INT_PTR>(GetPropW(control, L"MiniTranslator.RegionWidth")));
    const int previous_height = static_cast<int>(
        reinterpret_cast<INT_PTR>(GetPropW(control, L"MiniTranslator.RegionHeight")));
    const int previous_radius = static_cast<int>(
        reinterpret_cast<INT_PTR>(GetPropW(control, L"MiniTranslator.RegionRadius")));
    if (previous_width == width && previous_height == height &&
        previous_radius == radius) {
        return;
    }
    const int diameter = std::max(1, radius) * 2;
    HRGN region = CreateRoundRectRgn(0, 0, rect.right + 1, rect.bottom + 1,
                                    diameter, diameter);
    if (!region) return;
    if (!SetWindowRgn(control, region, TRUE)) {
        DeleteObject(region);
        return;
    }
    SetPropW(control, L"MiniTranslator.RegionWidth",
             reinterpret_cast<HANDLE>(static_cast<INT_PTR>(width)));
    SetPropW(control, L"MiniTranslator.RegionHeight",
             reinterpret_cast<HANDLE>(static_cast<INT_PTR>(height)));
    SetPropW(control, L"MiniTranslator.RegionRadius",
             reinterpret_cast<HANDLE>(static_cast<INT_PTR>(radius)));
}

void fill_round_rect(HDC dc, const RECT& rect, int radius,
                     COLORREF fill, COLORREF border) {
    static ID2D1Factory* factory = [] {
        ID2D1Factory* value = nullptr;
        D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &value);
        return value;
    }();
    ID2D1DCRenderTarget* target = nullptr;
    if (factory && rect.right > rect.left && rect.bottom > rect.top) {
        D2D1_RENDER_TARGET_PROPERTIES properties{};
        properties.type = D2D1_RENDER_TARGET_TYPE_DEFAULT;
        properties.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
        properties.pixelFormat.alphaMode = D2D1_ALPHA_MODE_IGNORE;
        properties.usage = D2D1_RENDER_TARGET_USAGE_NONE;
        properties.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
        if (SUCCEEDED(factory->CreateDCRenderTarget(&properties, &target)) &&
            SUCCEEDED(target->BindDC(dc, &rect))) {
            ID2D1SolidColorBrush* fill_brush = nullptr;
            ID2D1SolidColorBrush* border_brush = nullptr;
            target->CreateSolidColorBrush(
                D2D1::ColorF(GetRValue(fill) / 255.0F, GetGValue(fill) / 255.0F,
                             GetBValue(fill) / 255.0F),
                &fill_brush);
            target->CreateSolidColorBrush(
                D2D1::ColorF(GetRValue(border) / 255.0F, GetGValue(border) / 255.0F,
                             GetBValue(border) / 255.0F),
                &border_brush);
            target->BeginDraw();
            target->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
            const auto rounded = D2D1::RoundedRect(
                D2D1::RectF(0.5F, 0.5F,
                            static_cast<float>(rect.right - rect.left) - 0.5F,
                            static_cast<float>(rect.bottom - rect.top) - 0.5F),
                static_cast<float>(radius), static_cast<float>(radius));
            if (fill_brush) target->FillRoundedRectangle(rounded, fill_brush);
            if (border_brush) target->DrawRoundedRectangle(rounded, border_brush, 1.0F);
            const HRESULT drawn = target->EndDraw();
            if (fill_brush) fill_brush->Release();
            if (border_brush) border_brush->Release();
            target->Release();
            if (SUCCEEDED(drawn)) return;
            target = nullptr;
        }
    }
    if (target) target->Release();
    HBRUSH brush = CreateSolidBrush(fill);
    HPEN pen = CreatePen(PS_SOLID, 1, border);
    if (!brush || !pen) {
        if (pen) DeleteObject(pen);
        if (brush) DeleteObject(brush);
        return;
    }
    const auto old_brush = SelectObject(dc, brush);
    const auto old_pen = SelectObject(dc, pen);
    RoundRect(dc, rect.left, rect.top, rect.right, rect.bottom,
              radius * 2, radius * 2);
    SelectObject(dc, old_pen);
    SelectObject(dc, old_brush);
    DeleteObject(pen);
    DeleteObject(brush);
}

using ui_draw::draw_text;

void draw_settings_nav_icon(HDC dc, int page, const RECT& bounds,
                            COLORREF color, HFONT icon_font) {
    if (page < 0 || page >= static_cast<int>(navigation_glyphs.size()) || !icon_font) return;
    const wchar_t glyph = navigation_glyphs[static_cast<std::size_t>(page)];
    draw_text(dc, std::wstring_view(&glyph, 1), bounds, icon_font, color,
              DT_CENTER | DT_VCENTER | DT_SINGLELINE);
}

HFONT create_icon_font(int dpi) {
    constexpr std::array<const wchar_t*, 3> faces{
        L"Segoe Fluent Icons", L"Segoe MDL2 Assets", L"Segoe UI Symbol",
    };
    for (const auto* face : faces) {
        HFONT candidate = CreateFontW(
            -px(17, dpi), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, face);
        if (!candidate) continue;
        HDC dc = GetDC(nullptr);
        wchar_t actual[LF_FACESIZE]{};
        if (dc) {
            const auto old_font = SelectObject(dc, candidate);
            GetTextFaceW(dc, static_cast<int>(std::size(actual)), actual);
            SelectObject(dc, old_font);
            ReleaseDC(nullptr, dc);
        }
        if (_wcsicmp(actual, face) == 0 || face == faces.back()) return candidate;
        DeleteObject(candidate);
    }
    return nullptr;
}

void clear_combo_edit_selection(HWND combo) noexcept {
    if (!combo) return;
    const int length = GetWindowTextLengthW(combo);
    SendMessageW(combo, CB_SETEDITSEL, 0, MAKELPARAM(length, length));
    COMBOBOXINFO info{sizeof(COMBOBOXINFO)};
    if (GetComboBoxInfo(combo, &info) && info.hwndItem) {
        const int edit_length = GetWindowTextLengthW(info.hwndItem);
        SendMessageW(info.hwndItem, EM_SETSEL, edit_length, edit_length);
    }
}

void set_combo_edit_redraw(HWND combo, bool enabled) noexcept {
    if (!combo) return;
    COMBOBOXINFO info{sizeof(info)};
    if (!GetComboBoxInfo(combo, &info) || !info.hwndItem ||
        info.hwndItem == combo) {
        return;
    }
    if (!enabled) {
        SendMessageW(info.hwndItem, WM_SETREDRAW, FALSE, 0);
        clear_combo_edit_selection(combo);
        return;
    }
    clear_combo_edit_selection(combo);
    SendMessageW(info.hwndItem, WM_SETREDRAW, TRUE, 0);
    InvalidateRect(info.hwndItem, nullptr, FALSE);
}

COLORREF blend_color(COLORREF base, COLORREF tint, int tint_percent) noexcept {
    const int amount = std::clamp(tint_percent, 0, 100);
    const auto channel = [amount](BYTE base_value, BYTE tint_value) {
        return static_cast<BYTE>((base_value * (100 - amount) + tint_value * amount) / 100);
    };
    return RGB(channel(GetRValue(base), GetRValue(tint)),
               channel(GetGValue(base), GetGValue(tint)),
               channel(GetBValue(base), GetBValue(tint)));
}

void style_combo_popup(HWND combo, int dpi, HFONT font) noexcept {
    if (!combo) return;
    SendMessageW(combo, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), px(30, dpi));
    SendMessageW(combo, CB_SETITEMHEIGHT, 0, px(30, dpi));
    SendMessageW(combo, CB_SETMINVISIBLE, 8, 0);

    RECT combo_rect{};
    GetWindowRect(combo, &combo_rect);
    int dropped_width = std::max(1, static_cast<int>(combo_rect.right - combo_rect.left));
    HDC dc = GetDC(combo);
    HGDIOBJ old_font = nullptr;
    if (dc && font) old_font = SelectObject(dc, font);
    const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    for (int index = 0; dc && index < count; ++index) {
        const int length = static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, index, 0));
        if (length <= 0) continue;
        std::wstring value(static_cast<std::size_t>(length) + 1, L'\0');
        SendMessageW(combo, CB_GETLBTEXT, index, reinterpret_cast<LPARAM>(value.data()));
        SIZE measured{};
        if (GetTextExtentPoint32W(dc, value.data(), length, &measured)) {
            dropped_width = std::max(
                dropped_width, static_cast<int>(measured.cx) + px(54, dpi));
        }
    }
    if (dc) {
        if (old_font) SelectObject(dc, old_font);
        ReleaseDC(combo, dc);
    }
    SendMessageW(combo, CB_SETDROPPEDWIDTH,
                 std::min(dropped_width, px(460, dpi)), 0);

    COMBOBOXINFO info{sizeof(info)};
    if (!GetComboBoxInfo(combo, &info) || !info.hwndList) return;
    theme::apply_window_theme(info.hwndList);
    const LONG_PTR ex_style = GetWindowLongPtrW(info.hwndList, GWL_EXSTYLE);
    SetWindowLongPtrW(info.hwndList, GWL_EXSTYLE,
                      ex_style & ~static_cast<LONG_PTR>(WS_EX_CLIENTEDGE));
    SetWindowPos(info.hwndList, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE |
                     SWP_FRAMECHANGED);
}


int selected_index(HWND combo) {
    const LRESULT value = SendMessageW(combo, CB_GETCURSEL, 0, 0);
    return value == CB_ERR ? 0 : static_cast<int>(value);
}

void select_combo(HWND combo, int index) {
    SendMessageW(combo, CB_SETCURSEL, std::max(0, index), 0);
}

void populate_text_targets(HWND combo) {
    SendMessageW(combo, CB_RESETCONTENT, 0, 0);
    SendMessageW(combo, CB_ADDSTRING, 0,
                 reinterpret_cast<LPARAM>(L"自动选择"));
    for (const auto target : text_translation_targets()) {
        const auto label = target_display_name(target);
        SendMessageW(combo, CB_ADDSTRING, 0,
                     reinterpret_cast<LPARAM>(label.c_str()));
    }
    select_combo(combo, 0);
}

int text_target_index(std::wstring_view target) {
    if (target.empty()) return 0;
    const auto& targets = text_translation_targets();
    const auto found = std::find(targets.begin(), targets.end(), target);
    return found == targets.end()
        ? 0 : static_cast<int>(std::distance(targets.begin(), found)) + 1;
}

std::wstring text_target_at(HWND combo) {
    const int selected = selected_index(combo);
    if (selected <= 0) return {};
    const auto& targets = text_translation_targets();
    const auto index = static_cast<std::size_t>(selected - 1);
    return index < targets.size() ? std::wstring(targets[index]) : std::wstring{};
}

std::wstring text_target_selector(const TextTranslationSession& session) {
    if (!session.target().empty()) return target_display_name(session.target());
    if (session.input().find_first_not_of(L" \t\r\n") == std::wstring::npos) {
        return L"自动选择";
    }
    return L"自动 · " + target_display_name(session.effective_target());
}

std::wstring text_translation_status(const TextTranslationSession& session) {
    switch (session.state()) {
    case TextTranslationState::idle:
        return L"等待输入中...";
    case TextTranslationState::waiting:
        return L"将在停止输入 500 毫秒后翻译";
    case TextTranslationState::translating:
        return L"正在翻译为" + target_display_name(session.effective_target()) + L"…";
    case TextTranslationState::ready:
        return L"已翻译为" + target_display_name(session.effective_target());
    case TextTranslationState::error:
        return session.error().empty() ? L"文字翻译失败" : session.error();
    case TextTranslationState::too_long:
        return L"最多支持 5000 个字符，请删减后重试";
    }
    return {};
}

bool is_modifier_key(UINT key) noexcept {
    return key == VK_CONTROL || key == VK_LCONTROL || key == VK_RCONTROL ||
           key == VK_MENU || key == VK_LMENU || key == VK_RMENU ||
           key == VK_SHIFT || key == VK_LSHIFT || key == VK_RSHIFT ||
           key == VK_LWIN || key == VK_RWIN;
}

std::wstring hotkey_key_name(UINT key) {
    if ((key >= L'A' && key <= L'Z') || (key >= L'0' && key <= L'9')) {
        return std::wstring(1, static_cast<wchar_t>(key));
    }
    if (key >= VK_F1 && key <= VK_F24) {
        return L"F" + std::to_wstring(key - VK_F1 + 1);
    }
    switch (key) {
    case VK_SPACE: return L"Space";
    case VK_TAB: return L"Tab";
    case VK_RETURN: return L"Enter";
    case VK_BACK: return L"Backspace";
    case VK_DELETE: return L"Delete";
    case VK_INSERT: return L"Insert";
    case VK_HOME: return L"Home";
    case VK_END: return L"End";
    case VK_PRIOR: return L"PageUp";
    case VK_NEXT: return L"PageDown";
    case VK_UP: return L"Up";
    case VK_DOWN: return L"Down";
    case VK_LEFT: return L"Left";
    case VK_RIGHT: return L"Right";
    default: return {};
    }
}

bool autostart_enabled() {
    DWORD bytes = 0;
    const LSTATUS status = RegGetValueW(
        HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        L"MiniTranslator", RRF_RT_REG_SZ, nullptr, nullptr, &bytes);
    return status == ERROR_SUCCESS && bytes > sizeof(wchar_t);
}

void set_autostart(bool enabled) {
    HKEY key{};
    const LSTATUS opened = RegCreateKeyExW(
        HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Run",
        0, nullptr, 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (opened != ERROR_SUCCESS) throw_last_error("open autostart registry", opened);
    struct RegistryKey {
        HKEY value{};
        ~RegistryKey() { if (value) RegCloseKey(value); }
    } guard{key};
    if (!enabled) {
        const LSTATUS removed = RegDeleteValueW(key, L"MiniTranslator");
        if (removed != ERROR_SUCCESS && removed != ERROR_FILE_NOT_FOUND) {
            throw_last_error("disable autostart", removed);
        }
        return;
    }
    const auto command = L"\"" + executable_path().wstring() + L"\"";
    const DWORD bytes = static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t));
    const LSTATUS written = RegSetValueExW(
        key, L"MiniTranslator", 0, REG_SZ,
        reinterpret_cast<const BYTE*>(command.c_str()), bytes);
    if (written != ERROR_SUCCESS) throw_last_error("enable autostart", written);
}

std::wstring ocr_language_name(std::wstring_view tag) {
    if (tag.starts_with(L"zh-Hans")) return L"简体中文";
    if (tag.starts_with(L"zh-Hant")) return L"繁体中文";
    if (tag.starts_with(L"en")) return L"英语";
    if (tag.starts_with(L"ja")) return L"日语";
    if (tag.starts_with(L"ko")) return L"韩语";
    if (tag.starts_with(L"fr")) return L"法语";
    if (tag.starts_with(L"de")) return L"德语";
    if (tag.starts_with(L"es")) return L"西班牙语";
    if (tag.starts_with(L"ru")) return L"俄语";
    if (tag.starts_with(L"it")) return L"意大利语";
    if (tag.starts_with(L"pt")) return L"葡萄牙语";
    return std::wstring(tag);
}


bool same_hotkey(const HotkeySpec& left, const HotkeySpec& right) noexcept {
    return left.modifiers == right.modifiers &&
           left.virtual_key == right.virtual_key;
}


}  // namespace minitrans::settings_internal
