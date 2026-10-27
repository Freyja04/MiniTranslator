#pragma once

#include <winrt/Windows.Data.Json.h>

// wingdi.h maps GetObject to GetObjectW, which would also rewrite the
// C++/WinRT IJsonValue::GetObject method used below.
#ifdef GetObject
#undef GetObject
#endif

#include <string_view>

namespace minitrans::json_field {

// The string member `name`, or `fallback` when the object is null, the key is
// missing, or the value has another type. Every free-provider response and
// stored profile reads through here, so one wrong-shape reply cannot throw.
inline std::wstring string(const winrt::Windows::Data::Json::JsonObject& object,
                           std::wstring_view name,
                           std::wstring_view fallback = {}) {
    try {
        if (object && object.HasKey(name)) {
            const auto value = object.Lookup(name);
            if (value &&
                value.ValueType() == winrt::Windows::Data::Json::JsonValueType::String) {
                return value.GetString().c_str();
            }
        }
    } catch (...) {
    }
    return std::wstring(fallback);
}

// The boolean member `name`, or `fallback` under the same conditions.
inline bool boolean(const winrt::Windows::Data::Json::JsonObject& object,
                    std::wstring_view name, bool fallback) {
    try {
        if (object && object.HasKey(name)) {
            const auto value = object.Lookup(name);
            if (value &&
                value.ValueType() == winrt::Windows::Data::Json::JsonValueType::Boolean) {
                return value.GetBoolean();
            }
        }
    } catch (...) {
    }
    return fallback;
}

}  // namespace minitrans::json_field
