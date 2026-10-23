#pragma once

// Shared helpers for the window self-tests. They sample the process resource
// usage so a window that forgets to release GDI objects, USER objects, threads
// or private bytes fails the self-test immediately instead of only showing up
// as slow handle growth after long use.

#include <windows.h>

#include <dwmapi.h>
#include <psapi.h>
#include <tlhelp32.h>

#include <array>
#include <cstddef>
#include <string>

namespace minitrans::self_test_support {

struct ProcessResourceSnapshot {
    DWORD gdi_objects{};
    DWORD user_objects{};
    DWORD threads{};
    SIZE_T private_bytes{};
};

inline DWORD process_thread_count() noexcept {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return 0;
    THREADENTRY32 entry{sizeof(entry)};
    DWORD count = 0;
    if (Thread32First(snapshot, &entry)) {
        const DWORD process = GetCurrentProcessId();
        do {
            if (entry.th32OwnerProcessID == process) ++count;
        } while (Thread32Next(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return count;
}

inline SIZE_T process_private_bytes() noexcept {
    using GetMemoryInfo = BOOL(WINAPI*)(HANDLE, PPROCESS_MEMORY_COUNTERS, DWORD);
    const HMODULE kernel = GetModuleHandleW(L"kernel32.dll");
    if (!kernel) return 0;
    const auto function = reinterpret_cast<GetMemoryInfo>(
        GetProcAddress(kernel, "K32GetProcessMemoryInfo"));
    if (!function) return 0;
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    return function(GetCurrentProcess(),
                    reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&counters),
                    sizeof(counters))
        ? counters.PrivateUsage : 0;
}

inline ProcessResourceSnapshot process_resources() noexcept {
    return ProcessResourceSnapshot{
        GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS),
        GetGuiResources(GetCurrentProcess(), GR_USEROBJECTS),
        process_thread_count(), process_private_bytes(),
    };
}

// Accepts a short warm-up (the first batches may allocate caches) and only
// fails on sustained growth, so a one-off allocation does not break the test.
template <std::size_t Count>
inline bool resource_series_stable(
        const std::array<ProcessResourceSnapshot, Count>& samples) noexcept {
    static_assert(Count >= 5);
    constexpr DWORD total_gdi_tolerance = 8;
    constexpr DWORD total_user_tolerance = 4;
    constexpr DWORD total_thread_tolerance = 1;
    constexpr SIZE_T total_private_tolerance = 3U * 1024U * 1024U;
    constexpr SIZE_T per_batch_private_tolerance = 2U * 1024U * 1024U;

    constexpr std::size_t warmup_batches = 3;
    const auto& first = samples[warmup_batches];
    const auto& last = samples.back();
    if (last.gdi_objects > first.gdi_objects + total_gdi_tolerance ||
        last.user_objects > first.user_objects + total_user_tolerance ||
        (first.threads && last.threads > first.threads + total_thread_tolerance) ||
        (first.private_bytes &&
         last.private_bytes > first.private_bytes + total_private_tolerance)) {
        return false;
    }

    int significant_growth_streak = 0;
    int gdi_growth_streak = 0;
    int user_growth_streak = 0;
    int thread_growth_streak = 0;
    int private_growth_streak = 0;
    for (std::size_t index = 1; index < samples.size(); ++index) {
        const auto& before = samples[index - 1];
        const auto& after = samples[index];
        const bool significant_growth =
            after.gdi_objects > before.gdi_objects + 1 ||
            after.user_objects > before.user_objects + 1 ||
            (before.threads && after.threads > before.threads) ||
            (before.private_bytes &&
             after.private_bytes > before.private_bytes + per_batch_private_tolerance);
        significant_growth_streak = significant_growth
            ? significant_growth_streak + 1 : 0;
        if (significant_growth_streak >= 2) return false;

        gdi_growth_streak = after.gdi_objects > before.gdi_objects
            ? gdi_growth_streak + 1 : 0;
        user_growth_streak = after.user_objects > before.user_objects
            ? user_growth_streak + 1 : 0;
        thread_growth_streak = before.threads && after.threads > before.threads
            ? thread_growth_streak + 1 : 0;
        constexpr SIZE_T private_growth_floor = 128U * 1024U;
        private_growth_streak =
            before.private_bytes &&
            after.private_bytes > before.private_bytes + private_growth_floor
                ? private_growth_streak + 1 : 0;
        if (gdi_growth_streak >= 3 || user_growth_streak >= 3 ||
            thread_growth_streak >= 3 || private_growth_streak >= 3) {
            return false;
        }
    }
    return true;
}

inline std::string resource_change(const ProcessResourceSnapshot& before,
                                   const ProcessResourceSnapshot& after) {
    return " gdi=" + std::to_string(before.gdi_objects) + "->" +
           std::to_string(after.gdi_objects) + " user=" +
           std::to_string(before.user_objects) + "->" +
           std::to_string(after.user_objects) + " threads=" +
           std::to_string(before.threads) + "->" + std::to_string(after.threads) +
           " private=" + std::to_string(before.private_bytes) + "->" +
           std::to_string(after.private_bytes);
}

// Lets queued paint and timer work finish before a sample is taken. The sleep
// gives DWM time to release the surfaces it held for the previous frame.
inline void settle_self_test_ui() noexcept {
    DwmFlush();
    Sleep(500);
    MSG message{};
    while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    DwmFlush();
}

}  // namespace minitrans::self_test_support
