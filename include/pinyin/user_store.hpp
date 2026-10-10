#pragma once

#include "pinyin/lexicon.hpp"

namespace pinyin {

// Reads one complete published file without taking the writer transaction lock
// or creating storage. Atomic replacements can proceed while the file is open.
UserDictionary read_user_dictionary(const std::filesystem::path& path);

struct PersonalDictionaryFile {
    UserDictionary users;
    std::size_t records = 0;
};
// Import files may repeat a canonical key; keep its largest selection count.
// Known main/supplementary headers are rejected: weights are not usage counts.
PersonalDictionaryFile read_personal_dictionary_file(const std::filesystem::path& path);
std::size_t export_user_dictionary(const std::filesystem::path& user,
    const std::filesystem::path& target, bool overwrite = false);
struct UserMergeResult {
    std::size_t imported = 0, added = 0, updated = 0, unchanged = 0, total = 0;
    std::filesystem::path backup;
};
// Locks, rereads, backs up an existing file, then publishes the merged snapshot.
// Reimporting the same data is a no-op; counts are never summed.
UserMergeResult merge_user_dictionary(const std::filesystem::path& user,
    const std::filesystem::path& source);

// Holds an exclusive lock for the session to prevent lost updates between processes.
// Construct for a write transaction, not for a read-only lookup.
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
