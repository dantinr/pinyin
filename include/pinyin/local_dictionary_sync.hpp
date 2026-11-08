#pragma once
#include "pinyin/main_dictionary.hpp"

namespace pinyin {
inline constexpr std::uint64_t local_sync_interval_ms = 10 * 60 * 1000;
// Explicit opt-in: an upgrade never starts clearing personal records silently.
bool local_sync_enabled(const std::filesystem::path& user);
void set_local_sync_enabled(const std::filesystem::path& user, bool enabled);
std::filesystem::path local_sync_file(const std::filesystem::path& user, const wchar_t* suffix);
struct LocalSyncStatus {
    std::uint64_t attempt_ms = 0;
    std::string success, message;
};
LocalSyncStatus read_local_sync_status(const std::filesystem::path& user);
enum class LocalSyncOutcome { skipped, success, failure };
LocalSyncOutcome synchronize_local_dictionary(const std::filesystem::path& base,
    const std::filesystem::path& user, std::uint64_t now_ms);
// Maintains a separate, current-user login entry; no cloud setting is changed.
void launch_local_sync_worker(const std::filesystem::path& user, bool enabled);
void remove_local_sync_startup();
}
