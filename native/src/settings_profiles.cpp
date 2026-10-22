#include "settings_profiles.hpp"

#include <algorithm>

namespace minitrans::settings_profiles {

namespace {

bool taken(const std::vector<Profile>& list, std::wstring_view name) {
    return std::any_of(list.begin(), list.end(), [&](const Profile& profile) {
        return profile.name == name;
    });
}

}  // namespace

std::wstring resolve_selection(const std::vector<Profile>& list,
                               std::wstring_view wanted) {
    if (!wanted.empty()) {
        for (const auto& profile : list) {
            if (profile.name == wanted) return profile.name;
        }
    }
    return list.empty() ? std::wstring() : list.front().name;
}

std::wstring next_default_name(const std::vector<Profile>& list) {
    int suffix = static_cast<int>(list.size()) + 1;
    std::wstring name = L"接口 " + std::to_wstring(suffix);
    while (taken(list, name)) {
        ++suffix;
        name = L"接口 " + std::to_wstring(suffix);
    }
    return name;
}

std::wstring name_after_removal(const std::vector<Profile>& list,
                                std::size_t removed) {
    if (list.empty()) return {};
    const std::size_t index = std::min(removed, list.size() - 1);
    return list[index].name;
}

std::wstring unique_rename(const std::vector<Profile>& list, std::size_t index,
                           std::wstring_view wanted) {
    if (wanted.empty()) return {};
    const std::wstring base(wanted);
    const auto conflicts = [&](std::wstring_view candidate) {
        for (std::size_t other = 0; other < list.size(); ++other) {
            if (other != index && list[other].name == candidate) return true;
        }
        return false;
    };
    std::wstring name = base;
    int suffix = 2;
    while (conflicts(name)) {
        name = base + L" " + std::to_wstring(suffix++);
    }
    return name;
}

std::size_t clamp_index(int index, std::size_t size) {
    if (size == 0 || index < 0) return 0;
    const std::size_t value = static_cast<std::size_t>(index);
    return value < size ? value : size - 1;
}

}  // namespace minitrans::settings_profiles
