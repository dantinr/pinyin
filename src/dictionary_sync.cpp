#include "pinyin/dictionary_sync.hpp"
#include "pinyin/input_statistics.hpp"
#include "statistics_file.hpp"
#include "sync_storage.hpp"
#include <sstream>

namespace pinyin {
namespace {
constexpr const char* archive_header = "# private-pinyin sync v1: date<TAB>text<TAB>pinyin<TAB>selection_count";
void same_settings(const std::filesystem::path& user, const SyncSettings& settings) {
    const auto current = read_sync_settings(user);
    if (!current.enabled || current.url != settings.url || current.token != settings.token)
        throw std::runtime_error("sync settings changed; the previous transfer was stopped");
}
std::uint64_t selection_count(const std::string& text) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("invalid sync selection count");
    const auto count = std::stoull(text);
    if (!count || count > 1000000000) throw std::runtime_error("sync selection count out of range");
    return count;
}
}
SyncArchive parse_sync_archive(const std::string& bytes) {
    if (bytes.size() > sync_storage::maximum_bytes) throw std::runtime_error("sync archive exceeds 32 MiB");
    sync_storage::wide(bytes);
    std::istringstream stream(bytes);
    std::string line;
    if (!std::getline(stream, line)) throw std::runtime_error("sync URL did not return a dictionary archive");
    if (line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (line != archive_header) throw std::runtime_error("unsupported sync archive; HTML and ordinary dictionary files are not sync archives");
    SyncArchive archive;
    std::size_t number = 1;
    while (std::getline(stream, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        try {
            std::istringstream row(line); std::string date, text, pronunciation, count, extra;
            if (!std::getline(row, date, '\t') || !std::getline(row, text, '\t') || !std::getline(row, pronunciation, '\t') ||
                !std::getline(row, count, '\t') || std::getline(row, extra, '\t') || line.back() == '\t')
                throw std::runtime_error("expected exactly four TAB-separated fields");
            statistics_file::validate_date(date); validate_text(text);
            auto& target = archive[date][{normalize_pronunciation(pronunciation), text}];
            target = std::max(target, selection_count(count));
        } catch (const std::exception& error) {
            throw std::runtime_error("sync archive line " + std::to_string(number) + ": " + error.what());
        }
    }
    return archive;
}
std::string serialize_sync_archive(const SyncArchive& archive) {
    std::ostringstream stream; stream << archive_header << '\n';
    for (const auto& day : archive) {
        statistics_file::validate_date(day.first);
        for (const auto& word : day.second) {
            validate_text(word.first.second);
            if (normalize_pronunciation(word.first.first) != word.first.first || !word.second || word.second > 1000000000)
                throw std::runtime_error("invalid sync dictionary record");
            stream << day.first << '\t' << word.first.second << '\t' << word.first.first << '\t' << word.second << '\n';
        }
    }
    auto bytes = stream.str();
    if (bytes.size() > sync_storage::maximum_bytes) throw std::runtime_error("sync archive exceeds 32 MiB");
    return bytes;
}
UserDictionary sync_archive_words(const SyncArchive& archive) {
    UserDictionary words;
    for (const auto& day : archive) for (const auto& word : day.second)
        words[word.first] = std::max(words[word.first], word.second);
    return words;
}
SyncResult synchronize_dictionary(const std::filesystem::path& user, const SyncSettings& settings,
    const SyncTransport& transport, const std::string& requested_date) {
    if (!settings.enabled) throw std::runtime_error("dictionary sync is disabled");
    validate_sync_settings(settings);
    const auto date = requested_date.empty() ? local_statistics_date() : requested_date;
    statistics_file::validate_date(date);
    UserStore transfer_lock(sync_file(user, L"transfer"));
    for (int attempt = 0; attempt < 4; ++attempt) {
        same_settings(user, settings);
        const auto reply = transport("GET", settings, {}, {});
        const bool missing = reply.status == 404;
        if (!missing && reply.status != 200) throw std::runtime_error("sync download returned HTTP " + std::to_string(reply.status));
        if (!missing && (reply.etag.size() < 2 || reply.etag.front() != '"' || reply.etag.back() != '"' ||
            reply.etag.size() > 512 || reply.etag.find_first_of("\r\n") != std::string::npos))
            throw std::runtime_error("sync server must provide a strong ETag and support conditional PUT");
        auto archive = missing ? SyncArchive{} : parse_sync_archive(reply.body);
        const auto remote = sync_archive_words(archive);
        const auto local = read_user_dictionary(user);
        SyncResult result;
        for (const auto& word : local) {
            const auto found = remote.find(word.first);
            if (found == remote.end() || found->second < word.second) {
                archive[date][word.first] = word.second; ++result.uploaded;
            }
        }
        same_settings(user, settings);
        if (missing || result.uploaded) {
            const auto uploaded = transport("PUT", settings, serialize_sync_archive(archive), missing ? "*" : reply.etag);
            if (uploaded.status == 412 || uploaded.status == 409) continue;
            if (uploaded.status != 200 && uploaded.status != 201 && uploaded.status != 204)
                throw std::runtime_error("sync upload returned HTTP " + std::to_string(uploaded.status));
        }
        same_settings(user, settings);
        const auto merged = sync_archive_words(archive);
        std::ostringstream source;
        source << "# private-pinyin user dictionary v1: text<TAB>pinyin<TAB>selection_count\n";
        for (const auto& word : merged) source << word.first.second << '\t' << word.first.first << '\t' << word.second << '\n';
        const auto incoming = sync_file(user, L"incoming");
        sync_storage::write(incoming, source.str());
        struct Cleanup { std::filesystem::path path; ~Cleanup() { std::error_code error; std::filesystem::remove(path, error); } } cleanup{incoming};
        result.downloaded = merge_user_dictionary(user, incoming);
        return result;
    }
    throw std::runtime_error("sync conflict: another device kept updating the archive; retry later");
}
}
