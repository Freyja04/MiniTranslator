#include "config.hpp"

#include "dpapi.hpp"
#include "util.hpp"

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <algorithm>
#include <array>
#include <cwctype>
#include <string_view>

#ifndef MINITRANS_UPDATE_MANIFEST_URL
#define MINITRANS_UPDATE_MANIFEST_URL L""
#endif
#ifndef MINITRANS_UPDATE_REPOSITORY_URL
#define MINITRANS_UPDATE_REPOSITORY_URL L""
#endif
#ifndef MINITRANS_UPDATE_CHANNEL
#define MINITRANS_UPDATE_CHANNEL L"stable"
#endif

namespace minitrans {

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

enum class KeyOutcome { not_secret, transformed, unreadable };

KeyOutcome transform_key(JsonObject options, bool encrypting) {
    if (!options || !options.HasKey(L"key")) {
        return KeyOutcome::not_secret;
    }
    const auto value = options.Lookup(L"key");
    if (!value || value.ValueType() != JsonValueType::String) {
        return KeyOutcome::not_secret;
    }
    const auto current = wide_to_utf8(value.GetString().c_str());
    const auto transformed = encrypting ? dpapi::encrypt(current) : dpapi::decrypt(current);
    // A protected value that cannot be unlocked (another user profile, another
    // machine) must be reported instead of silently becoming an empty key.
    const bool unreadable = !encrypting && !current.empty() && transformed.empty();
    options.SetNamedValue(L"key", JsonValue::CreateStringValue(utf8_to_wide(transformed)));
    return unreadable ? KeyOutcome::unreadable : KeyOutcome::transformed;
}

bool transform_secrets(JsonObject root, bool encrypting) {
    bool unreadable = false;
    if (!root.HasKey(L"translator")) {
        return false;
    }
    const auto translator = root.GetNamedObject(L"translator", nullptr);
    if (!translator) {
        return false;
    }
    for (const auto& entry : translator) {
        const auto value = entry.Value();
        if (value && value.ValueType() == JsonValueType::Object) {
            unreadable = transform_key(value.GetObject(), encrypting) == KeyOutcome::unreadable ||
                         unreadable;
        }
    }
    // Every user-defined large-model endpoint stores its own key, so the array
    // has to be walked element by element.
    const auto profiles = translator.GetNamedArray(L"ai_profiles", nullptr);
    if (!profiles) {
        return unreadable;
    }
    for (std::uint32_t index = 0; index < profiles.Size(); ++index) {
        const auto item = profiles.GetAt(index);
        if (item && item.ValueType() == JsonValueType::Object) {
            unreadable = transform_key(item.GetObject(), encrypting) == KeyOutcome::unreadable ||
                         unreadable;
        }
    }
    return unreadable;
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

std::wstring zero_padded(unsigned value, int width) {
    std::wstring text = std::to_wstring(value);
    while (static_cast<int>(text.size()) < width) {
        text.insert(text.begin(), L'0');
    }
    return text;
}

std::wstring recovery_suffix() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    return L".broken-" + zero_padded(now.wYear, 4) + zero_padded(now.wMonth, 2) +
           zero_padded(now.wDay, 2) + L"-" + zero_padded(now.wHour, 2) +
           zero_padded(now.wMinute, 2) + zero_padded(now.wSecond, 2);
}

std::filesystem::path backup_target(const std::filesystem::path& path) {
    auto candidate = path;
    candidate += recovery_suffix();
    std::error_code error;
    for (int attempt = 2; attempt <= 9; ++attempt) {
        if (!std::filesystem::exists(candidate, error) || error) {
            return candidate;
        }
        candidate = path;
        candidate += recovery_suffix() + L"-" + std::to_wstring(attempt);
    }
    return candidate;
}

// Keeps the bytes of a configuration that could not be used. Renaming is
// preferred because it also stops the next save from overwriting the original.
std::filesystem::path preserve_config(const std::filesystem::path& path, bool move) {
    std::error_code error;
    if (!std::filesystem::exists(path, error) || error) {
        return {};
    }
    const auto target = backup_target(path);
    if (move) {
        std::filesystem::rename(path, target, error);
        if (!error) {
            return target;
        }
        error.clear();
    }
    std::filesystem::copy_file(path, target,
                               std::filesystem::copy_options::overwrite_existing, error);
    return error ? std::filesystem::path{} : target;
}

}  // namespace

ConfigStore::ConfigStore() : ConfigStore(default_config_path()) {}

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
        "languages":["zh-Hans-CN","en-US"],
        "upscale":true
      },
      "translator":{
        "provider":"microsoft_free",
        "tencent_free":{},"microsoft_free":{},"iciba_free":{},
        "yandex_free":{},
        "deepl":{"key":"","free_plan":true},
        "ai_profiles":[]
      },
      "lang":{"zh_target":"en","target":"zh-Hans"},
      "appearance":{
        "auto_copy":true,
        "theme":"system"
      },
      "updates":{"manifest_url":"","repository_url":"","channel":"stable"},
      "autostart":false
    })json");
    auto updates = result.GetNamedObject(L"updates");
    updates.SetNamedValue(L"manifest_url", JsonValue::CreateStringValue(MINITRANS_UPDATE_MANIFEST_URL));
    updates.SetNamedValue(L"repository_url", JsonValue::CreateStringValue(MINITRANS_UPDATE_REPOSITORY_URL));
    updates.SetNamedValue(L"channel", JsonValue::CreateStringValue(MINITRANS_UPDATE_CHANNEL));
    return result;
}

void ConfigStore::load() {
    root_ = defaults();
    recovery_backup_.clear();
    recovery_note_.clear();
    bool rewrite_migrated_values = false;
    try {
        if (std::filesystem::exists(path_)) {
            const auto disk = parse_json(read_utf8_file(path_));
            rewrite_migrated_values = contains_plaintext_secrets(disk);
            deep_merge(root_, disk);
        }
    } catch (...) {
        // Reading or parsing failed: keep the bytes and fall back to defaults.
        recovery_backup_ = preserve_config(path_, true);
        recovery_note_ = L"配置文件无法读取，已恢复默认设置";
        root_ = defaults();
    }
    // Update provenance is a build-time trust boundary, never user configuration.
    root_.SetNamedValue(L"updates", defaults().GetNamedObject(L"updates"));
    if (transform_secrets(root_, false)) {
        // The file is still valid JSON, but some keys belong to another user or
        // machine. Copy it aside before a later save replaces the ciphertext.
        if (recovery_backup_.empty()) {
            recovery_backup_ = preserve_config(path_, false);
        }
        if (recovery_note_.empty()) {
            recovery_note_ = L"配置中的接口密钥无法解密，请重新填写";
        }
    }

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
        // The per-row enable switch replaced translator.ai_selected: an older
        // file that picked one endpoint turns that row on, and the retired key
        // is dropped so the two sources of truth cannot drift apart.
        const winrt::hstring selected_key(L"ai_selected");
        if (translators.HasKey(selected_key)) {
            const std::wstring selected =
                translators.GetNamedString(selected_key, L"").c_str();
            translators.Remove(selected_key);
            rewrite_migrated_values = true;
            if (configured_provider == L"ai" && !selected.empty()) {
                if (const auto profiles =
                        translators.GetNamedArray(L"ai_profiles", nullptr)) {
                    for (std::uint32_t index = 0; index < profiles.Size(); ++index) {
                        const auto item = profiles.GetAt(index);
                        if (!item || item.ValueType() != JsonValueType::Object) continue;
                        const auto profile = item.GetObject();
                        if (profile.GetNamedString(L"name", L"") != selected) continue;
                        profile.SetNamedValue(L"enabled", JsonValue::CreateBooleanValue(true));
                    }
                }
            }
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
    // Windows OCR is the only recognizer, so the former engine selector is gone.
    if (const auto ocr = root_.GetNamedObject(L"ocr", nullptr)) {
        const winrt::hstring engine(L"engine");
        if (ocr.HasKey(engine)) {
            ocr.Remove(engine);
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

std::wstring ConfigStore::recovery_message() const {
    std::wstring message = recovery_note_;
    if (!recovery_backup_.empty()) {
        message += L"；原文件已备份为 " + recovery_backup_.filename().wstring();
    }
    return message;
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

void config_self_test() {
    std::error_code error;
    auto directory = std::filesystem::temp_directory_path(error);
    if (error) {
        directory = std::filesystem::current_path(error);
        if (error) {
            throw AppError("config self-test cannot find a working directory");
        }
    }
    directory /= L"MiniTranslator-config-self-test";
    std::filesystem::remove_all(directory, error);
    std::filesystem::create_directories(directory, error);
    if (error) {
        throw AppError("config self-test cannot create its working directory");
    }

    // A corrupt file is moved aside and reported, never silently replaced.
    const auto corrupt = directory / L"config.json";
    const char* const corrupt_text = R"({"hotkey":"Ctrl+Alt+Q",)";
    write_utf8_file_atomic(corrupt, corrupt_text);
    {
        ConfigStore recovered(corrupt);
        if (!recovered.recovered() || recovered.recovery_backup().empty() ||
            !std::filesystem::exists(recovered.recovery_backup()) ||
            std::filesystem::exists(corrupt)) {
            throw AppError("config self-test did not preserve an unreadable configuration");
        }
        if (read_utf8_file(recovered.recovery_backup()) != corrupt_text) {
            throw AppError("config self-test backup lost the original bytes");
        }
        if (recovered.string(L"hotkey") != L"Ctrl+Alt+Q") {
            throw AppError("config self-test did not fall back to defaults");
        }
    }

    // A healthy file is used as written and must not report a recovery.
    const auto healthy = directory / L"healthy.json";
    write_utf8_file_atomic(healthy, R"({"appearance":{"theme":"dark"}})");
    {
        ConfigStore loaded(healthy);
        if (loaded.recovered() || loaded.string(L"appearance.theme") != L"dark") {
            throw AppError("config self-test mishandled a healthy configuration");
        }
    }

    // Secrets survive a save/load cycle without ever reaching the disk in clear.
    const auto secrets_path = directory / L"secrets.json";
    {
        ConfigStore secrets(secrets_path);
        secrets.set_string(L"translator.deepl.key", L"deepl-self-test-key");
        secrets.save();
    }
    if (read_utf8_file(secrets_path).find("deepl-self-test-key") != std::string::npos) {
        throw AppError("config self-test wrote an unprotected secret");
    }
    {
        ConfigStore reloaded(secrets_path);
        if (reloaded.recovered() ||
            reloaded.string(L"translator.deepl.key") != L"deepl-self-test-key") {
            throw AppError("config self-test failed the DPAPI round trip");
        }
    }

    std::filesystem::remove_all(directory, error);
}

}  // namespace minitrans
