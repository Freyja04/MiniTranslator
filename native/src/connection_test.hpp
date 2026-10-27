#pragma once

#include <windows.h>
#include <winrt/Windows.Data.Json.h>

#include <string>
#include <thread>
#include <utility>

namespace minitrans::connection_test {

// One probe outcome: whether the endpoint answered plus the line shown under
// the field. Posted back to the window that started the probe.
using Result = std::pair<bool, std::wstring>;

// Runs the "translate Hello" probe for `provider` on a worker thread and posts
// the outcome to `target` as `message_id`: wparam is 1 on success and lparam is
// a heap Result the handler must delete. The settings page and both dialogs
// share this so the three 「测试连接」 buttons cannot drift apart.
[[nodiscard]] std::jthread run(HWND target, UINT message_id,
                               std::wstring provider,
                               winrt::Windows::Data::Json::JsonObject options);

// Same, for a call site that keeps its options as JSON text.
[[nodiscard]] std::jthread run(HWND target, UINT message_id,
                               std::wstring provider, std::wstring options_json);

// Deletes results that arrived for a window that is going away. Call it before
// destroying the window so a late post cannot leak its payload.
void discard(HWND target, UINT message_id) noexcept;

}  // namespace minitrans::connection_test
