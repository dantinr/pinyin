#include "pinyin/learning_dictionary.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <stdexcept>

namespace pinyin {
std::filesystem::path default_user_path() {
    const auto size = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
    if (!size) throw std::runtime_error("LOCALAPPDATA is unavailable; specify --user");
    std::wstring value(size, L'\0');
    const auto copied = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), size);
    if (!copied || copied >= size) throw std::runtime_error("cannot read LOCALAPPDATA");
    value.resize(copied);
    return std::filesystem::path(value) / L"PrivatePinyin" / L"words.user.tsv";
}
std::filesystem::path ime_learning_flag(const std::filesystem::path& user_path) {
    auto flag = user_path; flag += L".ime-learning.disabled"; return flag;
}
bool ime_learning_enabled(const std::filesystem::path& user_path) {
    if (user_path.empty()) return false;
    const auto attributes = GetFileAttributesW(ime_learning_flag(user_path).c_str());
    if (attributes != INVALID_FILE_ATTRIBUTES) return false;
    const auto error = GetLastError();
    // A missing setting uses the default. An unreadable setting must not bypass
    // an explicit opt-out and start saving personal words.
    return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND;
}
void set_ime_learning(const std::filesystem::path& user_path, bool enabled) {
    if (user_path.empty()) throw std::runtime_error("user dictionary path is unavailable");
    const auto flag = ime_learning_flag(user_path);
    if (enabled) { std::filesystem::remove(flag); return; }
    std::filesystem::create_directories(flag.parent_path());
    const auto file = CreateFileW(flag.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) throw std::runtime_error("cannot disable local IME learning");
    CloseHandle(file);
}
namespace {
bool learnable(const InputResult& result) {
    if (result.action != InputAction::commit || result.pronunciation.empty()) return false;
    validate_text(result.text);
    // Bound learned phrases; raw ASCII, cancelled preedits and long sentences
    // cannot become records in the automatic word dictionary.
    if (result.text.size() % 3 || result.text.size() > 16 * 3) return false;
    for (std::size_t i = 0; i < result.text.size(); i += 3) {
        const auto a = static_cast<unsigned char>(result.text[i]);
        const auto b = static_cast<unsigned char>(result.text[i + 1]);
        const auto c = static_cast<unsigned char>(result.text[i + 2]);
        if ((a & 0xf0) != 0xe0) return false;
        const auto code = ((a & 0x0f) << 12) | ((b & 0x3f) << 6) | (c & 0x3f);
        if (code < 0x4e00 || code > 0x9fff) return false;
    }
    const auto pronunciation = normalize_pronunciation(result.pronunciation);
    const auto syllables = static_cast<std::size_t>(std::count(pronunciation.begin(), pronunciation.end(), ' ') + 1);
    return syllables == result.text.size() / 3;
}
}
LearningDictionary::LearningDictionary(const std::filesystem::path& base_path, std::filesystem::path user_path)
    : user_path_(std::move(user_path)) {
    base_.load(base_path); active_ = base_;
}
Lexicon LearningDictionary::merged(const UserDictionary& users) const {
    auto lexicon = base_;
    for (const auto& word : users) lexicon.add({word.first.second, word.first.first, 1});
    return lexicon;
}
void LearningDictionary::refresh() {
    enabled_ = ime_learning_enabled(user_path_);
    if (!enabled_) {
        if (!users_.empty()) active_ = base_;
        users_.clear(); available_ = true; return;
    }
    try {
        UserDictionary users;
        { UserStore store(user_path_); users = store.load(); }
        auto active = merged(users);
        users_ = std::move(users); active_ = std::move(active);
    } catch (const std::exception&) {
        users_.clear(); active_ = base_; available_ = false;
    }
}
bool LearningDictionary::remember(const InputResult& result) noexcept {
    try {
        if (!ime_learning_enabled(user_path_) || !learnable(result)) return false;
        // Re-read under the same lock used to save: never overwrite another app's
        // additions with this service's older in-memory snapshot.
        UserDictionary users;
        {
            UserStore store(user_path_, 100);
            users = store.load();
            Candidate word; word.text = result.text; word.pronunciation = result.pronunciation;
            learn(users, word);
            store.save(users);
        }
        auto active = merged(users);
        users_ = std::move(users); active_ = std::move(active); available_ = true;
        return true;
    } catch (const std::exception&) {
        available_ = false; return false; // A dictionary failure must never prevent typing.
    }
}
}
