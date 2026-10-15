#pragma once
#include "pinyin/user_store.hpp"
#include <functional>

namespace pinyin {
struct SyncSettings {
    bool enabled = false;
    std::string url;
    std::string token;
};
struct SyncStatus {
    std::string attempt;
    std::string success;
    std::string message;
};
using SyncArchive = std::map<std::string, UserDictionary>;
struct SyncReply {
    unsigned status = 0;
    std::string etag;
    std::string body;
};
using SyncTransport = std::function<SyncReply(const std::string&, const SyncSettings&,
    const std::string&, const std::string&)>;
struct SyncResult {
    std::size_t uploaded = 0;
    UserMergeResult downloaded;
};
std::filesystem::path sync_file(const std::filesystem::path& user, const wchar_t* suffix);
SyncSettings read_sync_settings(const std::filesystem::path& user);
void save_sync_settings(const std::filesystem::path& user, const SyncSettings& settings);
SyncStatus read_sync_status(const std::filesystem::path& user);
void save_sync_status(const std::filesystem::path& user, const SyncStatus& status);
void validate_sync_settings(const SyncSettings& settings);
SyncArchive parse_sync_archive(const std::string& bytes);
std::string serialize_sync_archive(const SyncArchive& archive);
UserDictionary sync_archive_words(const SyncArchive& archive);
SyncReply sync_http_request(const std::string& method, const SyncSettings& settings,
    const std::string& body, const std::string& condition);
SyncResult synchronize_dictionary(const std::filesystem::path& user, const SyncSettings& settings,
    const SyncTransport& transport = sync_http_request, const std::string& date = {});
void launch_sync_worker(const std::filesystem::path& user, bool enabled);
void remove_sync_startup();
}
