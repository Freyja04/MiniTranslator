#include "config.hpp"

#include "dpapi.hpp"
#include "util.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cwctype>
#include <limits>
#include <string_view>

#ifndef SCREENTRANS_UPDATE_MANIFEST_URL
#define SCREENTRANS_UPDATE_MANIFEST_URL L""
#endif
#ifndef SCREENTRANS_UPDATE_REPOSITORY_URL
#define SCREENTRANS_UPDATE_REPOSITORY_URL L""
#endif
#ifndef SCREENTRANS_UPDATE_CHANNEL
#define SCREENTRANS_UPDATE_CHANNEL L"stable"
#endif

namespace screentrans {

using namespace winrt::Windows::Data::Json;

namespace {

JsonObject parse_json(std::string_view utf8) {
    return JsonObject::Parse(utf8_to_wide(utf8));
}

JsonObject deep_copy(const JsonObject& source) {
    return JsonObject::Parse(source.Stringify());
}

void deep_merge(JsonObject destination, const JsonObject& override) {
    for (const auto& entry : override) {
        const auto key = entry.Key();
        const auto incoming = entry.Value();
        if (incoming && incoming.ValueType() == JsonValueType::Object &&
            destination.HasKey(key)) {
            const auto current = destination.Lookup(key);
            if (current && current.ValueType() == JsonValueType::Object) {
                deep_merge(current.GetObject(), incoming.GetObject());
                continue;
            }
        }
        destination.Insert(key, incoming);
    }
}

void transform_key(JsonObject options, bool encrypting) {
    if (!options || !options.HasKey(L"key")) {
        return;
    }
    const auto value = options.Lookup(L"key");
    if (!value || value.ValueType() != JsonValueType::String) {
        return;
    }
    const auto current = wide_to_utf8(value.GetString().c_str());
    const auto transformed = encrypting ? dpapi::encrypt(current) : dpapi::decrypt(current);
    options.SetNamedValue(L"key", JsonValue::CreateStringValue(utf8_to_wide(transformed)));
}

void transform_secrets(JsonObject root, bool encrypting) {
    if (!root.HasKey(L"translator")) {
        return;
    }
    const auto translator = root.GetNamedObject(L"translator", nullptr);
    if (!translator) {
        return;
    }
    for (const auto& entry : translator) {
        const auto value = entry.Value();
        if (value && value.ValueType() == JsonValueType::Object) {
            transform_key(value.GetObject(), encrypting);
        }
    }
    // Every user-defined large-model endpoint stores its own key, so the array
    // has to be walked element by element.
    const auto profiles = translator.GetNamedArray(L"ai_profiles", nullptr);
    if (!profiles) {
        return;
    }
    for (std::uint32_t index = 0; index < profiles.Size(); ++index) {
        const auto item = profiles.GetAt(index);
        if (item && item.ValueType() == JsonValueType::Object) {
            transform_key(item.GetObject(), encrypting);
        }
    }
}

bool contains_plaintext_key(const JsonObject& options) {
    if (!options || !options.HasKey(L"key")) return false;
    const auto value = options.Lookup(L"key");
    if (!value || value.ValueType() != JsonValueType::String) return false;
    const auto key = wide_to_utf8(value.GetString().c_str());
    return !key.empty() && !key.starts_with(dpapi::prefix);
}

bool contains_plaintext_secrets(const JsonObject& root) {
    if (root.HasKey(L"translator")) {
        const auto translator = root.GetNamedObject(L"translator", nullptr);
        if (translator) {
            for (const auto& entry : translator) {
                const auto value = entry.Value();
                if (value && value.ValueType() == JsonValueType::Object &&
                    contains_plaintext_key(value.GetObject())) {
                    return true;
                }
            }
            const auto profiles = translator.GetNamedArray(L"ai_profiles", nullptr);
            if (profiles) {
                for (std::uint32_t index = 0; index < profiles.Size(); ++index) {
                    const auto item = profiles.GetAt(index);
                    if (item && item.ValueType() == JsonValueType::Object &&
                        contains_plaintext_key(item.GetObject())) {
                        return true;
                    }
                }
            }
        }
    }
    return false;
}

std::vector<std::wstring_view> split_path(std::wstring_view path) {
    std::vector<std::wstring_view> parts;
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find(L'.', start);
        const auto part = path.substr(start, end == std::wstring_view::npos
            ? path.size() - start
            : end - start);
        if (!part.empty()) {
            parts.push_back(part);
        }
        if (end == std::wstring_view::npos) {
            break;
        }
        start = end + 1;
    }
    return parts;
}

}  // namespace

ConfigStore::ConfigStore()
    : path_(app_data_directory() / L"config.json"), root_(defaults()) {
    load();
}

ConfigStore::ConfigStore(const std::filesystem::path& path)
    : path_(path), root_(defaults()) {
    load();
}

JsonObject ConfigStore::defaults() {
    auto result = parse_json(R"json({
      "hotkey":"Ctrl+Alt+Q",
      "hotkey_toggle":"Ctrl+Alt+W",
      "hotkey_text_translate":"Ctrl+Alt+Space",
      "ocr":{
        "engine":"windows",
        "languages":["zh-Hans-CN","en-US"],
        "upscale":true
      },
      "translator":{
        "provider":"microsoft_free",
        "tencent_free":{},"microsoft_free":{},"iciba_free":{},
        "yandex_free":{},
        "deepl":{"key":"","free_plan":true},
        "ai_profiles":[],
        "ai_selected":""
      },
      "lang":{"zh_target":"en"},
      "appearance":{
        "auto_copy":true,
        "theme":"system"
      },
      "updates":{"manifest_url":"","repository_url":"","channel":"stable"},
      "autostart":false
    })json");
    auto updates = result.GetNamedObject(L"updates");
    updates.SetNamedValue(L"manifest_url", JsonValue::CreateStringValue(SCREENTRANS_UPDATE_MANIFEST_URL));
    updates.SetNamedValue(L"repository_url", JsonValue::CreateStringValue(SCREENTRANS_UPDATE_REPOSITORY_URL));
    updates.SetNamedValue(L"channel", JsonValue::CreateStringValue(SCREENTRANS_UPDATE_CHANNEL));
    return result;
}

void ConfigStore::load() {
    root_ = defaults();
    bool rewrite_migrated_values = false;
    try {
        if (std::filesystem::exists(path_)) {
            const auto disk = parse_json(read_utf8_file(path_));
            rewrite_migrated_values = contains_plaintext_secrets(disk);
            deep_merge(root_, disk);
        }
    } catch (...) {
        root_ = defaults();
    }
    // Update provenance is a build-time trust boundary, never user configuration.
    root_.SetNamedValue(L"updates", defaults().GetNamedObject(L"updates"));
    transform_secrets(root_, false);

    // A config written by an older build may name an engine that no longer
    // exists. Keep such files usable by falling back to the default engine.
    constexpr std::array<std::wstring_view, 6> supported_providers{
        L"tencent_free", L"microsoft_free", L"iciba_free",
        L"yandex_free", L"deepl", L"ai",
    };
    if (const auto translators = root_.GetNamedObject(L"translator", nullptr)) {
        const std::wstring configured_provider =
            translators.GetNamedString(L"provider", L"").c_str();
        if (std::find(supported_providers.begin(), supported_providers.end(),
                      configured_provider) == supported_providers.end()) {
            translators.SetNamedValue(
                L"provider", JsonValue::CreateStringValue(L"microsoft_free"));
            rewrite_migrated_values = true;
        }
    }
    const auto appearance = root_.GetNamedObject(L"appearance");
    std::wstring configured_theme =
        appearance.GetNamedString(L"theme", L"system").c_str();
    std::transform(configured_theme.begin(), configured_theme.end(),
                   configured_theme.begin(),
                   [](wchar_t value) { return static_cast<wchar_t>(std::towlower(value)); });
    if (configured_theme != L"system" && configured_theme != L"light" &&
        configured_theme != L"dark") {
        appearance.SetNamedValue(
            L"theme", JsonValue::CreateStringValue(L"system"));
        rewrite_migrated_values = true;
    } else {
        const winrt::hstring current =
            appearance.GetNamedString(L"theme", L"system");
        if (std::wstring(current.c_str()) != configured_theme) {
            appearance.SetNamedValue(
                L"theme", JsonValue::CreateStringValue(configured_theme));
            rewrite_migrated_values = true;
        }
    }
    // Custom accent colors are gone; themes now own the shared accent.
    // Font selection is retired as well; every window follows the Windows UI font.
    for (const auto* retired : {L"close_mode", L"timeout_ms", L"accent",
                                L"font_family", L"min_font_px"}) {
        const winrt::hstring key(retired);
        if (appearance.HasKey(key)) {
            appearance.Remove(key);
            rewrite_migrated_values = true;
        }
    }
    if (rewrite_migrated_values) {
        try {
            save();
        } catch (...) {
            // A locked or read-only config must not prevent the tray app from
            // starting.  The in-memory migration still applies this session.
        }
    }
}

void ConfigStore::save() const {
    auto on_disk = deep_copy(root_);
    transform_secrets(on_disk, true);
    write_utf8_file_atomic(path_, wide_to_utf8(on_disk.Stringify().c_str()));
}

IJsonValue ConfigStore::lookup(std::wstring_view path) const {
    IJsonValue current = root_;
    for (const auto part : split_path(path)) {
        if (!current || current.ValueType() != JsonValueType::Object) {
            return nullptr;
        }
        const auto object_value = current.GetObject();
        const winrt::hstring key(part);
        if (!object_value.HasKey(key)) {
            return nullptr;
        }
        current = object_value.Lookup(key);
    }
    return current;
}

std::wstring ConfigStore::string(std::wstring_view path, std::wstring_view fallback) const {
    try {
        const auto value = lookup(path);
        if (value && value.ValueType() == JsonValueType::String) {
            return value.GetString().c_str();
        }
    } catch (...) {
    }
    return std::wstring(fallback);
}

bool ConfigStore::boolean(std::wstring_view path, bool fallback) const {
    try {
        const auto value = lookup(path);
        return value && value.ValueType() == JsonValueType::Boolean
            ? value.GetBoolean()
            : fallback;
    } catch (...) {
        return fallback;
    }
}

int ConfigStore::integer(std::wstring_view path, int fallback) const {
    try {
        const auto value = lookup(path);
        if (value && value.ValueType() == JsonValueType::Number) {
            const double number = value.GetNumber();
            if (std::isfinite(number) &&
                number >= static_cast<double>(std::numeric_limits<int>::min()) &&
                number <= static_cast<double>(std::numeric_limits<int>::max())) {
                return static_cast<int>(std::lround(number));
            }
        }
    } catch (...) {
    }
    return fallback;
}

std::vector<std::wstring> ConfigStore::strings(std::wstring_view path) const {
    std::vector<std::wstring> output;
    try {
        const auto value = lookup(path);
        if (!value || value.ValueType() != JsonValueType::Array) {
            return output;
        }
        for (const auto item : value.GetArray()) {
            if (item && item.ValueType() == JsonValueType::String) {
                output.emplace_back(item.GetString().c_str());
            }
        }
    } catch (...) {
    }
    return output;
}

JsonObject ConfigStore::object(std::wstring_view path) const {
    try {
        const auto value = lookup(path);
        if (value && value.ValueType() == JsonValueType::Object) {
            return value.GetObject();
        }
    } catch (...) {
    }
    return JsonObject{};
}

JsonArray ConfigStore::array(std::wstring_view path) const {
    try {
        const auto value = lookup(path);
        if (value && value.ValueType() == JsonValueType::Array) {
            return value.GetArray();
        }
    } catch (...) {
    }
    return JsonArray{};
}

JsonObject ConfigStore::parent_for(std::wstring_view path, std::wstring& leaf) {
    const auto parts = split_path(path);
    if (parts.empty()) {
        throw AppError("configuration path is empty");
    }
    leaf.assign(parts.back());
    auto current = root_;
    for (std::size_t index = 0; index + 1 < parts.size(); ++index) {
        const winrt::hstring key(parts[index]);
        if (!current.HasKey(key) || current.Lookup(key).ValueType() != JsonValueType::Object) {
            current.SetNamedValue(key, JsonObject{});
        }
        current = current.GetNamedObject(key);
    }
    return current;
}

void ConfigStore::set_string(std::wstring_view path, std::wstring_view value) {
    std::wstring leaf;
    parent_for(path, leaf).SetNamedValue(leaf, JsonValue::CreateStringValue(value));
}

void ConfigStore::set_boolean(std::wstring_view path, bool value) {
    std::wstring leaf;
    parent_for(path, leaf).SetNamedValue(leaf, JsonValue::CreateBooleanValue(value));
}

void ConfigStore::set_integer(std::wstring_view path, int value) {
    std::wstring leaf;
    parent_for(path, leaf).SetNamedValue(leaf, JsonValue::CreateNumberValue(value));
}

void ConfigStore::set_strings(std::wstring_view path,
                              const std::vector<std::wstring>& values) {
    JsonArray array;
    for (const auto& value : values) {
        array.Append(JsonValue::CreateStringValue(value));
    }
    std::wstring leaf;
    parent_for(path, leaf).SetNamedValue(leaf, array);
}

void ConfigStore::set_json(std::wstring_view path, const IJsonValue& value) {
    std::wstring leaf;
    parent_for(path, leaf).SetNamedValue(leaf, value);
}

}  // namespace screentrans
