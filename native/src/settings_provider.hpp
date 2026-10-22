#pragma once

#include "settings_catalog.hpp"

#include <cstddef>
#include <string_view>

namespace minitrans::settings_provider {

// The provider combo lists only the traditional (non large-model) rows of the
// catalog, so its selection index is a position among those rows. Load and save
// must map that index identically; keeping the mapping here makes the rule
// testable without a window.

[[nodiscard]] constexpr std::size_t traditional_count() noexcept {
    std::size_t count = 0;
    for (const auto& provider : settings_internal::providers) {
        if (provider.kind != settings_internal::ProviderInfo::Kind::ai) ++count;
    }
    return count;
}

[[nodiscard]] constexpr std::wstring_view default_id() noexcept {
    return L"microsoft_free";
}

// Out-of-range indexes (an empty combo reports -1) fall back to the first row,
// which is what the page showed before the index was validated.
[[nodiscard]] constexpr std::wstring_view id_at(std::size_t index) noexcept {
    std::size_t seen = 0;
    std::wstring_view first{};
    for (const auto& provider : settings_internal::providers) {
        if (provider.kind == settings_internal::ProviderInfo::Kind::ai) continue;
        if (seen == 0) first = provider.id;
        if (seen == index) return provider.id;
        ++seen;
    }
    return first;
}

inline constexpr std::size_t not_listed = static_cast<std::size_t>(-1);

[[nodiscard]] constexpr std::size_t index_of(std::wstring_view id) noexcept {
    std::size_t seen = 0;
    for (const auto& provider : settings_internal::providers) {
        if (provider.kind == settings_internal::ProviderInfo::Kind::ai) continue;
        if (provider.id == id) return seen;
        ++seen;
    }
    return not_listed;
}

// Which controls the "翻译接口" page shows for a provider. The large-model entry
// reuses the key field and adds the profile picker, protocol, endpoint and model
// fields; free providers hide everything that only paid endpoints need.
struct FieldVisibility {
    bool provider_combo{};
    bool profile_picker{};
    bool protocol{};
    bool key{};
    bool endpoint{};
    bool model{};
    bool model_refresh{};
};

[[nodiscard]] constexpr FieldVisibility fields_for(std::wstring_view id) noexcept {
    if (id == L"ai") {
        return {.provider_combo = false,
                .profile_picker = true,
                .protocol = true,
                .key = true,
                .endpoint = true,
                .model = true,
                .model_refresh = true};
    }
    bool needs_key = false;
    for (const auto& provider : settings_internal::providers) {
        if (provider.id != id) continue;
        needs_key = provider.kind != settings_internal::ProviderInfo::Kind::free;
        break;
    }
    return {.provider_combo = true,
            .profile_picker = false,
            .protocol = false,
            .key = needs_key,
            .endpoint = false,
            .model = false,
            .model_refresh = false};
}

}  // namespace minitrans::settings_provider
