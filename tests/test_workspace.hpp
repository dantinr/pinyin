#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace pinyin::testing {
// Overrides only this test process's environment; an activated IME cannot read
// the desktop user's settings or personal words during integration tests.
class TestWorkspace {
    std::filesystem::path temporary_base_;
    std::wstring original_appdata_;
    std::wstring original_profile_;
    bool had_appdata_ = false;
    bool had_profile_ = false;
public:
    std::filesystem::path root;
    TestWorkspace() {
        temporary_base_ = std::filesystem::weakly_canonical(std::filesystem::temp_directory_path());
        root = temporary_base_ / (L"private-pinyin-learning-" + std::to_wstring(GetCurrentProcessId()) +
            L"-" + std::to_wstring(GetTickCount64()));
        if (!std::filesystem::create_directory(root)) throw std::runtime_error("test directory already exists");
        const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
        had_appdata_ = size != 0;
        if (size) {
            original_appdata_.resize(size);
            original_appdata_.resize(GetEnvironmentVariableW(L"LOCALAPPDATA", original_appdata_.data(), size));
        }
        const auto profile_size = GetEnvironmentVariableW(L"USERPROFILE", nullptr, 0);
        had_profile_ = profile_size != 0;
        if (profile_size) {
            original_profile_.resize(profile_size);
            original_profile_.resize(GetEnvironmentVariableW(L"USERPROFILE", original_profile_.data(), profile_size));
        }
        if (!SetEnvironmentVariableW(L"LOCALAPPDATA", (root / L"appdata").c_str()) ||
            !SetEnvironmentVariableW(L"USERPROFILE", (root / L"profile").c_str())) {
            SetEnvironmentVariableW(L"LOCALAPPDATA", had_appdata_ ? original_appdata_.c_str() : nullptr);
            SetEnvironmentVariableW(L"USERPROFILE", had_profile_ ? original_profile_.c_str() : nullptr);
            std::filesystem::remove(root); throw std::runtime_error("cannot isolate test environment");
        }
    }
    TestWorkspace(const TestWorkspace&) = delete;
    TestWorkspace& operator=(const TestWorkspace&) = delete;
    ~TestWorkspace() {
        SetEnvironmentVariableW(L"LOCALAPPDATA", had_appdata_ ? original_appdata_.c_str() : nullptr);
        SetEnvironmentVariableW(L"USERPROFILE", had_profile_ ? original_profile_.c_str() : nullptr);
        // Only the absolute, newly created child of the verified temporary root.
        if (root.is_absolute() && root.parent_path() == temporary_base_) {
            std::error_code error; std::filesystem::remove_all(root, error);
        }
    }
};
}
