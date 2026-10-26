#include "diagnostics.hpp"

#include "util.hpp"

#include <deque>
#include <filesystem>
#include <mutex>

namespace minitrans::diagnostics {

namespace {

constexpr std::size_t ring_capacity = 64;

std::mutex& ring_mutex() {
    static std::mutex mutex;
    return mutex;
}

std::deque<std::wstring>& ring() {
    static std::deque<std::wstring> entries;
    return entries;
}

std::wstring timestamp() {
    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04u-%02u-%02u %02u:%02u:%02u.%03u", now.wYear,
               now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond,
               now.wMilliseconds);
    return buffer;
}

}  // namespace

void record(std::wstring_view event) {
    if (event.empty()) return;
    std::lock_guard lock(ring_mutex());
    auto& entries = ring();
    if (entries.size() == ring_capacity) {
        entries.pop_front();
    }
    entries.push_back(timestamp() + L" " + std::wstring(event));
}

std::wstring export_report() {
    std::wstring report;
    report += L"MiniTranslator diagnostics\r\n";
    report += L"generated: " + timestamp() + L"\r\n";
    wchar_t module_path[MAX_PATH + 1]{};
    const DWORD length = GetModuleFileNameW(nullptr, module_path, MAX_PATH);
    if (length > 0 && length < MAX_PATH) {
        report += L"executable: ";
        report += module_path;
        report += L"\r\n";
    }
    report += L"events (oldest first)\r\n";
    report += L"---------------------\r\n";
    {
        std::lock_guard lock(ring_mutex());
        if (ring().empty()) {
            report += L"(no events recorded)\r\n";
        }
        for (const auto& entry : ring()) {
            report += entry;
            report += L"\r\n";
        }
    }
    const auto directory = std::filesystem::temp_directory_path();
    const auto path = directory / L"MiniTranslator-diagnostics.txt";
    try {
        write_utf8_file_atomic(path, wide_to_utf8(report));
    } catch (...) {
        return {};
    }
    return path.wstring();
}

void clear_for_test() {
    std::lock_guard lock(ring_mutex());
    ring().clear();
}

}  // namespace minitrans::diagnostics
