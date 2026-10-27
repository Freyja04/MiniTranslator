#include "connection_test.hpp"

#include "http.hpp"
#include "translator.hpp"
#include "util.hpp"

#include <functional>
#include <new>

namespace minitrans::connection_test {

namespace {

using JsonObject = winrt::Windows::Data::Json::JsonObject;

std::jthread start(HWND target, UINT message_id, std::wstring provider,
                   std::function<JsonObject()> make_options) {
    return std::jthread(
        [target, message_id, provider = std::move(provider),
         make_options = std::move(make_options)](std::stop_token stop) noexcept {
            auto* raw = new (std::nothrow) Result;
            if (!raw) return;
            try {
                WinrtApartment apartment(winrt::apartment_type::multi_threaded);
                TranslatorService service;
                const auto translated = service.translate(
                    provider, make_options(), {L"Hello"}, L"zh-Hans",
                    std::nullopt, stop);
                raw->first = translated.size() == 1 && !translated.front().empty();
                raw->second = raw->first ? L"连接正常" : L"接口没有返回译文";
            } catch (const std::exception& error) {
                raw->first = false;
                raw->second = redact_sensitive(utf8_to_wide(error.what()));
            } catch (...) {
                raw->first = false;
                raw->second = L"连接测试遇到未知错误";
            }
            if (!PostMessageW(target, message_id, raw->first ? 1 : 0,
                              reinterpret_cast<LPARAM>(raw))) {
                delete raw;
            }
        });
}

}  // namespace

std::jthread run(HWND target, UINT message_id, std::wstring provider,
                 JsonObject options) {
    return start(target, message_id, std::move(provider),
                 [options = std::move(options)]() mutable { return options; });
}

std::jthread run(HWND target, UINT message_id, std::wstring provider,
                 std::wstring options_json) {
    return start(target, message_id, std::move(provider),
                 [options_json = std::move(options_json)] {
                     return JsonObject::Parse(options_json);
                 });
}

void discard(HWND target, UINT message_id) noexcept {
    if (!target) return;
    MSG pending{};
    while (PeekMessageW(&pending, target, message_id, message_id, PM_REMOVE)) {
        delete reinterpret_cast<Result*>(pending.lParam);
    }
}

}  // namespace minitrans::connection_test
