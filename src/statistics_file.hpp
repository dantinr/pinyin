#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <stdexcept>
#include <string>

namespace pinyin::statistics_file {
inline std::uint64_t number(const std::string& text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("invalid statistics number");
    return std::stoull(text);
}
inline void validate_date(const std::string& date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-')
        throw std::runtime_error("invalid statistics date");
    const auto year = number(date.substr(0, 4));
    const auto month = number(date.substr(5, 2));
    const auto day = number(date.substr(8, 2));
    constexpr unsigned lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < 1601 || year > 9999 || month < 1 || month > 12)
        throw std::runtime_error("invalid statistics date");
    const auto length = lengths[month - 1] + (month == 2 && year % 4 == 0 && (year % 100 != 0 || year % 400 == 0));
    if (!day || day > length) throw std::runtime_error("invalid statistics date");
}
inline std::uint64_t sum(std::uint64_t left, std::uint64_t right) {
    if (right > std::numeric_limits<std::uint64_t>::max() - left)
        throw std::runtime_error("statistics counter overflow");
    return left + right;
}
inline std::runtime_error error(const char* action) {
    return std::runtime_error(std::string(action) + " (Windows error " + std::to_string(GetLastError()) + ")");
}
inline std::string snapshot(const std::filesystem::path& path) {
    const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        if (GetLastError() == ERROR_FILE_NOT_FOUND || GetLastError() == ERROR_PATH_NOT_FOUND) return {};
        throw error("cannot read statistics");
    }
    struct Close { HANDLE file; ~Close() { CloseHandle(file); } } close{file};
    std::string bytes; std::array<char, 8192> buffer{};
    for (;;) {
        DWORD read = 0;
        if (!ReadFile(file, buffer.data(), static_cast<DWORD>(buffer.size()), &read, nullptr))
            throw error("cannot read statistics");
        if (!read) return bytes;
        bytes.append(buffer.data(), read);
    }
}
inline void publish(const std::filesystem::path& path, const std::string& bytes) {
    static std::atomic<unsigned long> sequence{0};
    auto temporary = path;
    temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) +
        L"." + std::to_wstring(++sequence);
    auto file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw error("cannot create statistics snapshot");
    try {
        for (std::size_t offset = 0; offset < bytes.size();) {
            const auto length = static_cast<DWORD>(std::min<std::size_t>(bytes.size() - offset, 1024 * 1024));
            DWORD written = 0;
            if (!WriteFile(file, bytes.data() + offset, length, &written, nullptr) || !written)
                throw error("cannot write statistics snapshot");
            offset += written;
        }
        if (!FlushFileBuffers(file)) throw error("cannot flush statistics snapshot");
        CloseHandle(file); file = INVALID_HANDLE_VALUE;
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            throw error("cannot publish statistics");
    } catch (...) {
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        DeleteFileW(temporary.c_str()); throw;
    }
}
}
