#include "pinyin/ime_settings.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdexcept>

namespace pinyin {
std::filesystem::path punctuation_flag(const std::filesystem::path& user_path) {
    auto flag = user_path; flag += L".punctuation.disabled"; return flag;
}
ImeSettings read_ime_settings(const std::filesystem::path& user_path) {
    ImeSettings result;
    result.learning = ime_learning_enabled(user_path);
    if (user_path.empty()) { result.chinese_punctuation = false; return result; }
    const auto attributes = GetFileAttributesW(punctuation_flag(user_path).c_str());
    const auto error = GetLastError();
    result.chinese_punctuation = attributes == INVALID_FILE_ATTRIBUTES &&
        (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND);
    return result;
}
void set_chinese_punctuation(const std::filesystem::path& user_path, bool enabled) {
    if (user_path.empty()) throw std::runtime_error("user dictionary path is unavailable");
    const auto flag = punctuation_flag(user_path);
    if (enabled) { std::filesystem::remove(flag); return; }
    std::filesystem::create_directories(flag.parent_path());
    const auto file = CreateFileW(flag.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot change punctuation setting");
    CloseHandle(file);
}
}
