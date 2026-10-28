#include "pinyin/ime_settings.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdexcept>
#include <charconv>
#include <cstdio>
#include "sync_storage.hpp"

namespace pinyin {
namespace {
bool default_enabled(const std::filesystem::path& flag) {
    const auto attributes = GetFileAttributesW(flag.c_str());
    const auto error = GetLastError();
    return attributes == INVALID_FILE_ATTRIBUTES &&
        (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
}
void set_enabled(const std::filesystem::path& flag, bool enabled) {
    if (enabled) { std::filesystem::remove(flag); return; }
    std::filesystem::create_directories(flag.parent_path());
    const auto file = CreateFileW(flag.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot change IME setting");
    CloseHandle(file);
}
}
std::filesystem::path punctuation_flag(const std::filesystem::path& user_path) {
    auto flag = user_path; flag += L".punctuation.disabled"; return flag;
}
ImeSettings read_ime_settings(const std::filesystem::path& user_path) {
    ImeSettings result;
    result.learning = ime_learning_enabled(user_path);
    if (user_path.empty()) { result.chinese_punctuation = result.automatic_english = false; return result; }
    result.chinese_punctuation = default_enabled(punctuation_flag(user_path));
    result.automatic_english = default_enabled(automatic_english_flag(user_path));
    const auto icon_attributes = GetFileAttributesW(icon_rotation_flag(user_path).c_str());
    result.rotate_icons = icon_attributes != INVALID_FILE_ATTRIBUTES && !(icon_attributes & FILE_ATTRIBUTE_DIRECTORY);
    return result;
}
void set_chinese_punctuation(const std::filesystem::path& user_path, bool enabled) {
    if (user_path.empty()) throw std::runtime_error("user dictionary path is unavailable");
    set_enabled(punctuation_flag(user_path), enabled);
}
std::filesystem::path automatic_english_flag(const std::filesystem::path& user_path) {
    auto flag = user_path; flag += L".automatic-english.disabled"; return flag;
}
void set_automatic_english(const std::filesystem::path& user_path, bool enabled) {
    if (user_path.empty()) throw std::runtime_error("user dictionary path is unavailable");
    set_enabled(automatic_english_flag(user_path), enabled);
}
std::filesystem::path icon_rotation_flag(const std::filesystem::path& user_path) {
    auto flag = user_path; flag += L".icon-rotation.enabled"; return flag;
}
void set_icon_rotation(const std::filesystem::path& user_path, bool enabled) {
    if (user_path.empty()) throw std::runtime_error("user dictionary path is unavailable");
    set_enabled(icon_rotation_flag(user_path), !enabled);
}
std::filesystem::path candidate_colors_path(const std::filesystem::path& user_path) {
    auto path = user_path; path += L".candidate-colors"; return path;
}
CandidateColors read_candidate_colors(const std::filesystem::path& user_path) noexcept {
    try {
        if (user_path.empty()) return {};
        const auto path = candidate_colors_path(user_path);
        const auto file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file == INVALID_HANDLE_VALUE) return {};
        char bytes[18]{}; DWORD count = 0;
        const bool read = !!ReadFile(file, bytes, sizeof(bytes), &count, nullptr);
        CloseHandle(file);
        if (!read || count != 17 || std::string(bytes, 3) != "v1 " || bytes[9] != ' ' || bytes[16] != '\n') return {};
        CandidateColors result;
        const auto background = std::from_chars(bytes + 3, bytes + 9, result.background, 16);
        const auto selection = std::from_chars(bytes + 10, bytes + 16, result.selection, 16);
        if (background.ec != std::errc{} || background.ptr != bytes + 9 ||
            selection.ec != std::errc{} || selection.ptr != bytes + 16) return {};
        result.custom = true; return result;
    } catch (...) { return {}; }
}
void set_candidate_colors(const std::filesystem::path& user_path, CandidateColors colors) {
    if (user_path.empty()) throw std::runtime_error("user dictionary path is unavailable");
    const auto path = candidate_colors_path(user_path);
    if (!colors.custom) { std::filesystem::remove(path); return; }
    if (colors.background > 0xffffff || colors.selection > 0xffffff)
        throw std::runtime_error("invalid candidate color");
    char bytes[18]{};
    std::snprintf(bytes, sizeof(bytes), "v1 %06x %06x\n",
        static_cast<unsigned>(colors.background), static_cast<unsigned>(colors.selection));
    sync_storage::write(path, bytes);
}
}
