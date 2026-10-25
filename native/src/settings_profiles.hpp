#pragma once

#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>

// wingdi.h maps GetObject to GetObjectW, which would also rewrite the
// C++/WinRT IJsonValue::GetObject method used by the parsers here.
#ifdef GetObject
#undef GetObject
#endif

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace minitrans::settings_profiles {

// One user-defined large-model endpoint. The name is the identity of the row,
// so it has to stay unique, and at most one row carries the enable flag.
struct Profile {
    std::wstring name;
    std::wstring protocol;
    std::wstring base_url;
    std::wstring key;
    std::wstring model;
    // At most one endpoint in the list carries this flag; it is what the
    // translation pipeline resolves to when the class is 「大模型翻译」.
    bool enabled{};
};

// The stored array and the wire options object share this shape, so the page,
// its add/edit popup and the pipeline all convert through here.
[[nodiscard]] std::vector<Profile> parse_profiles(
    const winrt::Windows::Data::Json::JsonArray& stored);
[[nodiscard]] winrt::Windows::Data::Json::JsonArray serialize_profiles(
    const std::vector<Profile>& list);
[[nodiscard]] winrt::Windows::Data::Json::JsonObject to_options(const Profile& profile);

// An endpoint is only usable once it has both an address and a model; the key
// stays optional because self-hosted gateways often allow anonymous calls.
[[nodiscard]] bool is_configured(const Profile& profile);

// Index of the row named `name`, or nullopt when the list lost that row. The
// name is the row's identity, so this is how the page finds the endpoint an
// edit dialog or an enable request refers to.
[[nodiscard]] std::optional<std::size_t> find_index(const std::vector<Profile>& list,
                                                    std::wstring_view name);

// Enable only `index`; every other row loses the flag. Used when the user
// switches an endpoint on, since one request channel can honour a single
// endpoint.
void select_enabled(std::vector<Profile>& list, std::size_t index);

// Drop the enable flag from rows that cannot serve requests and keep the first
// enabled row when a rewrite left several switched on, so the stored list is
// always consistent with the one-endpoint rule.
void normalize_enabled(std::vector<Profile>& list);

// "接口 N" name that no existing row uses yet.
[[nodiscard]] std::wstring next_default_name(const std::vector<Profile>& list);

// Rename target for the row at `index`: `wanted` made unique against the other
// rows with a " 2", " 3", ... suffix. An empty request yields an empty string,
// which callers treat as "nothing to rename".
[[nodiscard]] std::wstring unique_rename(const std::vector<Profile>& list,
                                         std::size_t index, std::wstring_view wanted);

}  // namespace minitrans::settings_profiles
