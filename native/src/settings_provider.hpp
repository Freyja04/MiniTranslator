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

// The page has two branches. A large-model endpoint never uses the provider
// combo or the key field: its address, key and model live in the endpoint rows,
// so the page shows the list instead. A traditional provider always shows the
// combo, and only a paid one shows the key field.
[[nodiscard]] constexpr bool uses_endpoint_list(std::wstring_view id) noexcept {
    return id == L"ai";
}

[[nodiscard]] constexpr bool needs_key(std::wstring_view id) noexcept {
    if (uses_endpoint_list(id)) return false;
    for (const auto& provider : settings_internal::providers) {
        if (provider.id != id) continue;
        return provider.kind != settings_internal::ProviderInfo::Kind::free;
    }
    return false;
}

}  // namespace minitrans::settings_provider
