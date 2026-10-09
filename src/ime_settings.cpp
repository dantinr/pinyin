#include "pinyin/ime_settings.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdexcept>

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
}
