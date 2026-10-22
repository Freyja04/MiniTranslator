#pragma once

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

// wingdi.h maps GetObject to GetObjectW. That macro would also rewrite the
// C++/WinRT IJsonValue::GetObject method name.
#ifdef GetObject
#undef GetObject
#endif

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace minitrans {

class ConfigStore {
public:
    ConfigStore();
    explicit ConfigStore(const std::filesystem::path& path);

    void load();
    void save() const;

    [[nodiscard]] std::wstring string(std::wstring_view path,
                                      std::wstring_view fallback = {}) const;
    [[nodiscard]] bool boolean(std::wstring_view path, bool fallback = false) const;
    [[nodiscard]] std::vector<std::wstring> strings(std::wstring_view path) const;
    [[nodiscard]] winrt::Windows::Data::Json::JsonObject object(
        std::wstring_view path) const;
    [[nodiscard]] winrt::Windows::Data::Json::JsonArray array(std::wstring_view path) const;

    void set_string(std::wstring_view path, std::wstring_view value);
    void set_boolean(std::wstring_view path, bool value);
    void set_strings(std::wstring_view path, const std::vector<std::wstring>& values);
    void set_json(std::wstring_view path,
                  const winrt::Windows::Data::Json::IJsonValue& value);

    [[nodiscard]] const winrt::Windows::Data::Json::JsonObject& root() const noexcept {
        return root_;
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    // A configuration that cannot be parsed is moved aside before the fallback
    // to defaults, so the next save can never destroy the user's own settings.
    [[nodiscard]] bool recovered() const noexcept { return !recovery_note_.empty(); }
    [[nodiscard]] std::wstring recovery_message() const;
    [[nodiscard]] const std::filesystem::path& recovery_backup() const noexcept {
        return recovery_backup_;
    }

    static winrt::Windows::Data::Json::JsonObject defaults();

private:
    winrt::Windows::Data::Json::IJsonValue lookup(std::wstring_view path) const;
    winrt::Windows::Data::Json::JsonObject parent_for(std::wstring_view path,
                                                       std::wstring& leaf);

    std::filesystem::path path_;
    winrt::Windows::Data::Json::JsonObject root_{nullptr};
    std::filesystem::path recovery_backup_;
    std::wstring recovery_note_;
};

// Headless checks for configuration recovery and DPAPI round tripping.
void config_self_test();

}  // namespace minitrans
