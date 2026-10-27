#include "settings_profiles.hpp"

#include "json_field.hpp"
#include "util.hpp"

#include <algorithm>

namespace minitrans::settings_profiles {

namespace {

bool taken(const std::vector<Profile>& list, std::wstring_view name) {
    return std::any_of(list.begin(), list.end(), [&](const Profile& profile) {
        return profile.name == name;
    });
}

constexpr std::wstring_view field_name = L"name";
constexpr std::wstring_view field_protocol = L"protocol";
constexpr std::wstring_view field_base_url = L"base_url";
constexpr std::wstring_view field_key = L"key";
constexpr std::wstring_view field_model = L"model";
constexpr std::wstring_view field_enabled = L"enabled";

auto string_field(const winrt::Windows::Data::Json::JsonObject& option,
                  std::wstring_view name) {
    return json_field::string(option, name);
}

auto flag_field(const winrt::Windows::Data::Json::JsonObject& option,
                std::wstring_view name, bool fallback) {
    return json_field::boolean(option, name, fallback);
}

}  // namespace

std::vector<Profile> parse_profiles(
    const winrt::Windows::Data::Json::JsonArray& stored) {
    std::vector<Profile> list;
    if (!stored) return list;
    for (const auto& entry : stored) {
        if (!entry ||
            entry.ValueType() != winrt::Windows::Data::Json::JsonValueType::Object) {
            continue;
        }
        const auto option = entry.GetObject();
        Profile profile;
        profile.name = trim(string_field(option, field_name));
        // Rows without a name cannot be addressed by the enable marker, so they
        // are dropped rather than shown as an unnamed endpoint.
        if (profile.name.empty()) continue;
        profile.protocol = string_field(option, field_protocol);
        if (profile.protocol.empty()) profile.protocol = L"openai_chat";
        profile.base_url = trim(string_field(option, field_base_url));
        profile.key = trim(string_field(option, field_key));
        profile.model = trim(string_field(option, field_model));
        profile.enabled = flag_field(option, field_enabled, false);
        list.push_back(std::move(profile));
    }
    return list;
}

winrt::Windows::Data::Json::JsonArray serialize_profiles(
    const std::vector<Profile>& list) {
    winrt::Windows::Data::Json::JsonArray array;
    for (const auto& profile : list) {
        array.Append(to_options(profile));
    }
    return array;
}

winrt::Windows::Data::Json::JsonObject to_options(const Profile& profile) {
    winrt::Windows::Data::Json::JsonObject option;
    option.SetNamedValue(field_name, winrt::Windows::Data::Json::JsonValue::CreateStringValue(profile.name));
    option.SetNamedValue(field_protocol, winrt::Windows::Data::Json::JsonValue::CreateStringValue(profile.protocol));
    option.SetNamedValue(field_base_url, winrt::Windows::Data::Json::JsonValue::CreateStringValue(profile.base_url));
    option.SetNamedValue(field_key, winrt::Windows::Data::Json::JsonValue::CreateStringValue(profile.key));
    option.SetNamedValue(field_model, winrt::Windows::Data::Json::JsonValue::CreateStringValue(profile.model));
    option.SetNamedValue(field_enabled, winrt::Windows::Data::Json::JsonValue::CreateBooleanValue(profile.enabled));
    return option;
}

bool is_configured(const Profile& profile) {
    return !profile.base_url.empty() && !profile.model.empty();
}

std::optional<std::size_t> find_index(const std::vector<Profile>& list,
                                      std::wstring_view name) {
    if (name.empty()) return std::nullopt;
    for (std::size_t index = 0; index < list.size(); ++index) {
        if (list[index].name == name) return index;
    }
    return std::nullopt;
}

std::wstring next_default_name(const std::vector<Profile>& list) {
    int suffix = static_cast<int>(list.size()) + 1;
    std::wstring name = L"接口 " + std::to_wstring(suffix);
    while (taken(list, name)) {
        ++suffix;
        name = L"接口 " + std::to_wstring(suffix);
    }
    return name;
}

std::wstring unique_rename(const std::vector<Profile>& list, std::size_t index,
                           std::wstring_view wanted) {
    if (wanted.empty()) return {};
    const std::wstring base(wanted);
    const auto conflicts = [&](std::wstring_view candidate) {
        for (std::size_t other = 0; other < list.size(); ++other) {
            if (other != index && list[other].name == candidate) return true;
        }
        return false;
    };
    std::wstring name = base;
    int suffix = 2;
    while (conflicts(name)) {
        name = base + L" " + std::to_wstring(suffix++);
    }
    return name;
}

void select_enabled(std::vector<Profile>& list, std::size_t index) {
    for (std::size_t row = 0; row < list.size(); ++row) {
        list[row].enabled = row == index && is_configured(list[row]);
    }
}

void normalize_enabled(std::vector<Profile>& list) {
    bool kept = false;
    for (auto& profile : list) {
        // An endpoint that lost its address or model cannot serve requests, and
        // only the first enabled row survives: one request channel, one endpoint.
        const bool usable = profile.enabled && is_configured(profile) && !kept;
        profile.enabled = usable;
        kept = kept || usable;
    }
}

}  // namespace minitrans::settings_profiles
