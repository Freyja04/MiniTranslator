#pragma once

#include "util.hpp"

#include <windows.h>

#include <optional>
#include <string>
#include <string_view>

namespace minitrans {

// --self-test runs every layer by default so CI keeps covering the whole app.
// While working on one window, narrowing the run to that layer cuts the wait
// from the full sweep to the single layer's own checks.
enum class SelfTestLayer : unsigned {
    none = 0,
    // No window at all: language tables, settings constants, hotkey parsing,
    // endpoint resolution and the text session's length rules.
    headless = 1u << 0,
    settings = 1u << 1,
    quick = 1u << 2,
    editor = 1u << 3,
    tray = 1u << 4,
    all = headless | settings | quick | editor | tray,
};

constexpr SelfTestLayer operator|(SelfTestLayer left, SelfTestLayer right) noexcept {
    return static_cast<SelfTestLayer>(static_cast<unsigned>(left) |
                                      static_cast<unsigned>(right));
}

constexpr bool has_layer(SelfTestLayer set, SelfTestLayer layer) noexcept {
    return (static_cast<unsigned>(set) & static_cast<unsigned>(layer)) != 0;
}

// Parses the value of --self-test=a,b. An unknown name yields nullopt so a
// typo fails loudly instead of silently running a narrower set than intended.
inline std::optional<SelfTestLayer> parse_self_test_layers(std::wstring_view value) {
    if (value.empty()) return std::nullopt;
    SelfTestLayer layers = SelfTestLayer::none;
    std::size_t start = 0;
    while (start <= value.size()) {
        const std::size_t comma = value.find(L',', start);
        const std::wstring_view name = value.substr(
            start, comma == std::wstring_view::npos ? std::wstring_view::npos
                                                    : comma - start);
        if (name == L"all") {
            layers = layers | SelfTestLayer::all;
        } else if (name == L"headless") {
            layers = layers | SelfTestLayer::headless;
        } else if (name == L"settings") {
            layers = layers | SelfTestLayer::settings;
        } else if (name == L"quick") {
            layers = layers | SelfTestLayer::quick;
        } else if (name == L"editor") {
            layers = layers | SelfTestLayer::editor;
        } else if (name == L"tray") {
            layers = layers | SelfTestLayer::tray;
        } else {
            return std::nullopt;
        }
        if (comma == std::wstring_view::npos) break;
        start = comma + 1;
    }
    return layers == SelfTestLayer::none ? std::nullopt : std::optional(layers);
}

inline const wchar_t* self_test_layer_name(SelfTestLayer layer) noexcept {
    switch (layer) {
    case SelfTestLayer::headless: return L"headless";
    case SelfTestLayer::settings: return L"settings";
    case SelfTestLayer::quick: return L"quick";
    case SelfTestLayer::editor: return L"editor";
    case SelfTestLayer::tray: return L"tray";
    case SelfTestLayer::all: return L"all";
    case SelfTestLayer::none: break;
    }
    return L"none";
}

// Timing goes to stderr, next to the failure report, so a narrow run shows
// which layer owns the wait without needing a profiler.
class SelfTestTimer {
public:
    explicit SelfTestTimer(SelfTestLayer layer) noexcept
        : layer_(layer), start_(GetTickCount64()) {}

    SelfTestTimer(const SelfTestTimer&) = delete;
    SelfTestTimer& operator=(const SelfTestTimer&) = delete;

    ~SelfTestTimer() {
        const HANDLE error = GetStdHandle(STD_ERROR_HANDLE);
        if (!error || error == INVALID_HANDLE_VALUE) return;
        const std::wstring line = std::wstring(L"[self-test] ") +
                                  self_test_layer_name(layer_) + L": " +
                                  std::to_wstring(GetTickCount64() - start_) + L" ms\r\n";
        const std::string utf8 = wide_to_utf8(line);
        DWORD written = 0;
        WriteFile(error, utf8.data(), static_cast<DWORD>(utf8.size()), &written, nullptr);
    }

private:
    SelfTestLayer layer_;
    ULONGLONG start_;
};

}  // namespace minitrans
