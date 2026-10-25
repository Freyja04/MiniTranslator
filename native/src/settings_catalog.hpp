#pragma once

#include <array>
#include <string_view>
#include <utility>

namespace minitrans::settings_internal {

// Translation providers offered on the "翻译接口" page. The "ai" row is the
// large-model entry: it is picked through the class selector at the top of the
// page instead of the provider combo, so every list shown in that combo skips
// it. Keep this table free of window types so the state layer can be tested
// without a HWND.
struct ProviderInfo {
    std::wstring_view id;
    std::wstring_view label;
    enum class Kind { free, deepl, ai } kind;
};

inline constexpr std::array<ProviderInfo, 6> providers{{
    {L"tencent_free", L"腾讯交互翻译（免密钥）", ProviderInfo::Kind::free},
    {L"microsoft_free", L"微软翻译（免密钥）", ProviderInfo::Kind::free},
    {L"iciba_free", L"词霸翻译（免密钥）", ProviderInfo::Kind::free},
    {L"yandex_free", L"Yandex 翻译（免密钥）", ProviderInfo::Kind::free},
    {L"deepl", L"DeepL", ProviderInfo::Kind::deepl},
    {L"ai", L"大模型翻译", ProviderInfo::Kind::ai},
}};

// The three wire formats a user-defined large-model endpoint can speak.
inline constexpr std::array<std::pair<std::wstring_view, std::wstring_view>, 3> ai_protocols{{
    {L"openai_chat", L"OpenAI Chat Completions"},
    {L"openai_responses", L"OpenAI Responses"},
    {L"anthropic_messages", L"Anthropic Messages"},
}};

// Short captions for the same table when it is drawn as one segmented row:
// the full labels would not fit three chips across the endpoint dialog card.
inline constexpr std::array<std::wstring_view, 3> ai_protocol_segments{{
    L"Chat Completions", L"Responses", L"Anthropic",
}};

static_assert(ai_protocol_segments.size() == ai_protocols.size());

}  // namespace minitrans::settings_internal
