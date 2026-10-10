#pragma once
#include "pinyin/input_session.hpp"
#include "pinyin/user_store.hpp"
#include "pinyin/dictionary_manager.hpp"

namespace pinyin {
std::filesystem::path default_user_path();
// Local learning is on unless this explicit opt-out marker exists.
std::filesystem::path ime_learning_flag(const std::filesystem::path& user_path);
bool ime_learning_enabled(const std::filesystem::path& user_path);
void set_ime_learning(const std::filesystem::path& user_path, bool enabled);

// Uses short read/merge/write transactions so different applications share words.
// Only remember a confirmed result after the application accepted its text.
class LearningDictionary {
public:
    LearningDictionary(const std::filesystem::path& base_path, std::filesystem::path user_path);
    void refresh();
    bool remember(const InputResult& result) noexcept;
    const Lexicon& lexicon() const noexcept { return active_; }
    const UserDictionary& users() const noexcept { return users_; }
    bool enabled() const noexcept { return enabled_; }
    bool available() const noexcept { return available_; }
private:
    Lexicon bundled_, base_, active_;
    SupplementaryDictionaries supplementary_;
    UserDictionary users_;
    std::filesystem::path user_path_;
    bool enabled_ = false;
    bool available_ = true;
    Lexicon merged(const UserDictionary& users) const;
};
}
