#pragma once

#include "pinyin/lexicon.hpp"

namespace pinyin {

// Holds an exclusive lock for the session to prevent lost updates between processes.
// Construct only when the user explicitly enables learning.
class UserStore {
public:
    explicit UserStore(std::filesystem::path path, unsigned lock_timeout_ms = 0);
    ~UserStore();
    UserStore(const UserStore&) = delete;
    UserStore& operator=(const UserStore&) = delete;
    UserDictionary load() const;
    // Writes a temporary file in the same directory, then atomically replaces the file.
    void save(const UserDictionary& users) const;
    const std::filesystem::path& path() const noexcept { return path_; }
private:
    std::filesystem::path path_;
    void* lock_ = nullptr;
};

} // namespace pinyin
