#pragma once

#include <windows.h>

#include <string>
#include <string_view>

namespace minitrans::diagnostics {

// A small in-memory ring of recent noteworthy events (unhandled errors,
// failed operations, state transitions worth debugging later). Release builds
// keep it too: the cost is a few string appends on rare paths, and it is what
// makes "导出诊断信息" meaningful without reproducing a bug first.
void record(std::wstring_view event);

// Dumps the ring (newest last) plus process metadata into a UTF-8 text file
// next to the user's temp directory; returns the path, or empty on failure.
[[nodiscard]] std::wstring export_report();

// Test hook: drops every recorded event.
void clear_for_test();

}  // namespace minitrans::diagnostics
