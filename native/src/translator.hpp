#pragma once

#include "http.hpp"

#include <winrt/Windows.Data.Json.h>
#include <winrt/Windows.Foundation.Collections.h>

#include <optional>
#include <stop_token>
#include <string>
#include <vector>

namespace screentrans {

class TranslateError : public AppError {
public:
    using AppError::AppError;
};

class TranslatorService {
public:
    TranslatorService() = default;

    std::vector<std::wstring> translate(
        std::wstring_view provider,
        const winrt::Windows::Data::Json::JsonObject& options,
        const std::vector<std::wstring>& texts,
        std::wstring_view target,
        const std::optional<std::wstring>& source = std::nullopt,
        std::stop_token stop = {});

    std::vector<std::wstring> list_models(
        const winrt::Windows::Data::Json::JsonObject& options,
        std::stop_token stop = {});

private:
    std::vector<std::wstring> translate_batch(
        std::wstring_view provider,
        const winrt::Windows::Data::Json::JsonObject& options,
        const std::vector<std::wstring>& texts,
        std::wstring_view target,
        const std::optional<std::wstring>& source,
        std::stop_token stop);
    std::wstring ai_call(
        const winrt::Windows::Data::Json::JsonObject& options,
        std::wstring_view system,
        std::wstring_view user,
        std::stop_token stop);
    std::vector<std::wstring> translate_ai(
        const winrt::Windows::Data::Json::JsonObject& options,
        const std::vector<std::wstring>& texts,
        std::wstring_view target,
        std::stop_token stop);
    HttpClient http_;
};

}  // namespace screentrans
