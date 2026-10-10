#pragma once
#include <filesystem>

namespace pinyin {
// AppData can be virtualized independently by each MSIX host. Keep shared IME
// data directly beneath USERPROFILE so all desktop hosts use the same files.
std::filesystem::path default_user_path();
// Import each physical legacy file once, preserving originals and snapshots.
// Explicit roots also let tests isolate both shared and per-package old stores.
void migrate_legacy_user_data(const std::filesystem::path& user,
    const std::filesystem::path& local_appdata);
}
