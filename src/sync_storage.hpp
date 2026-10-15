#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace pinyin::sync_storage {
inline constexpr std::size_t maximum_bytes = 32 * 1024 * 1024;
inline std::runtime_error failure(const char* action) {
    return std::runtime_error(std::string(action) + " (Windows error " + std::to_string(GetLastError()) + ")");
}
inline std::wstring wide(const std::string& text) {
    if (text.empty()) return {};
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!length) throw std::runtime_error("invalid UTF-8 in sync data");
    std::wstring result(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), result.data(), length);
    return result;
}
inline std::string read(const std::filesystem::path& path) {
    const auto handle = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return {};
        throw failure("cannot read sync file");
    }
    struct Close { HANDLE handle; ~Close() { CloseHandle(handle); } } close{handle};
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(handle, &length) || length.QuadPart < 0 || length.QuadPart > maximum_bytes)
        throw std::runtime_error("sync file exceeds 32 MiB");
    std::string result(static_cast<std::size_t>(length.QuadPart), '\0');
    std::size_t offset = 0;
    while (offset < result.size()) {
        DWORD copied = 0;
        if (!ReadFile(handle, result.data() + offset, static_cast<DWORD>(result.size() - offset), &copied, nullptr) || !copied)
            throw failure("cannot read complete sync file");
        offset += copied;
    }
    return result;
}
inline void write(const std::filesystem::path& path, const std::string& bytes) {
    if (bytes.size() > maximum_bytes) throw std::runtime_error("sync file exceeds 32 MiB");
    std::filesystem::create_directories(path.parent_path());
    static std::atomic<unsigned long> sequence{0};
    auto temporary = path;
    temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(++sequence);
    auto handle = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) throw failure("cannot create sync file");
    try {
        DWORD copied = 0;
        if (!WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &copied, nullptr) || copied != bytes.size() ||
            !FlushFileBuffers(handle)) throw failure("cannot save sync file");
        CloseHandle(handle); handle = INVALID_HANDLE_VALUE;
        const auto deadline = GetTickCount64() + 100;
        for (;;) {
            if (MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) break;
            const auto error = GetLastError();
            if ((error != ERROR_ACCESS_DENIED && error != ERROR_SHARING_VIOLATION) || GetTickCount64() >= deadline)
                throw failure("cannot publish sync file");
            Sleep(5);
        }
    } catch (...) {
        if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle);
        DeleteFileW(temporary.c_str()); throw;
    }
}
}
