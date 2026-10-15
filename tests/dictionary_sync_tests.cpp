#include "pinyin/dictionary_sync.hpp"
#include "test_workspace.hpp"
#include <fstream>
#include <iostream>

namespace {
using namespace pinyin;
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
template<class Action> void rejects(Action action, const char* message) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; } check(rejected, message);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary); return {std::istreambuf_iterator<char>(stream), {}};
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary); if (!(stream << bytes)) throw std::runtime_error("fixture write failed");
}
void save(const std::filesystem::path& user, const UserDictionary& words) { UserStore store(user); store.save(words, false); }
struct Server {
    bool exists = false, conflict = false;
    int revision = 0, gets = 0, puts = 0;
    SyncArchive archive;
    std::function<void()> during_put;
    SyncReply request(const std::string& method, const SyncSettings&, const std::string& body, const std::string& condition) {
        const auto etag = "\"" + std::to_string(revision) + "\"";
        if (method == "GET") { ++gets; return exists ? SyncReply{200, etag, serialize_sync_archive(archive)} : SyncReply{404, {}, {}}; }
        ++puts;
        if (during_put) { auto action = std::move(during_put); during_put = {}; action(); }
        if (conflict) {
            conflict = false; exists = true; ++revision;
            archive["2026-10-14"][{"chong qing", "重庆"}] = 8;
            return {412, {}, {}};
        }
        if ((condition == "*" && exists) || (condition != "*" && (!exists || condition != etag))) return {412, {}, {}};
        archive = parse_sync_archive(body); exists = true; ++revision; return {204, {}, {}};
    }
};
}
int main() {
    try {
        testing::TestWorkspace workspace;
        const auto user = workspace.root / L"device-a.tsv", other = workspace.root / L"device-b.tsv";
        check(!read_sync_settings(user).enabled && !std::filesystem::exists(sync_file(user, L"settings")), "sync default created storage or enabled networking");
        SyncSettings settings{true, "https://example.invalid/dictionary.tsv", "test-secret-token"};
        save_sync_settings(user, settings);
        const auto restored = read_sync_settings(user);
        check(restored.enabled && restored.url == settings.url && restored.token == settings.token, "sync settings did not round trip");
        const auto config = read(sync_file(user, L"settings"));
        check(config.find(settings.token) == std::string::npos, "sync token was stored without Windows protection");
        for (const auto& url : {"file:///C:/words", "https://user:secret@example.com/a", "https://example.com/a#fragment", "https://example.com/a\r\nX: bad", ""}) {
            auto invalid = settings; invalid.url = url;
            rejects([&] { save_sync_settings(user, invalid); }, "invalid sync URL was saved");
        }
        auto invalid_token = settings; invalid_token.token = "token\r\nX: bad";
        rejects([&] { save_sync_settings(user, invalid_token); }, "token header injection was accepted");
        check(read(sync_file(user, L"settings")) == config, "rejected settings overwrote the previous config");
        const auto header = serialize_sync_archive({});
        const auto parsed = parse_sync_archive("\xef\xbb\xbf" + header + "2026-10-14\t重庆\tCHONG QING\t2\r\n2026-10-14\t重庆\tchong qing\t5\n");
        check(parsed.at("2026-10-14").at({"chong qing", "重庆"}) == 5, "canonical duplicate counts were summed or lost");
        check(serialize_sync_archive(parse_sync_archive(serialize_sync_archive(parsed))) == serialize_sync_archive(parsed), "archive round trip changed records");
        for (const auto& bytes : {std::string("<html>error</html>"), std::string(), header + "2026-02-30\t你好\tni hao\t1\n",
            header + "2026-10-15\t你好\tni hao\t0\n", header + "2026-10-15\t你好\tni hao\t1000000001\n",
            header + "2026-10-15\t你好\tnihao\t1\n", header + "2026-10-15\t你好\tni hao\t1\textra\n",
            header + "2026-10-15\t你好\tni hao\t1\t\n", header + "\xff"})
            rejects([&] { parse_sync_archive(bytes); }, "corrupt sync archive was accepted");
        rejects([&] { parse_sync_archive(std::string(32 * 1024 * 1024 + 1, 'x')); }, "oversized archive was accepted");
        Server server;
        const SyncTransport transport = [&](const auto& method, const auto& config_value, const auto& body, const auto& condition) {
            return server.request(method, config_value, body, condition);
        };
        save(user, {{{"ni hao", "你好"}, 3}});
        auto result = synchronize_dictionary(user, settings, transport, "2026-10-14");
        check(result.uploaded == 1 && result.downloaded.added == 0 && server.archive.at("2026-10-14").size() == 1, "initial local upload failed");
        const auto puts = server.puts;
        result = synchronize_dictionary(user, settings, transport, "2026-10-15");
        check(result.uploaded == 0 && server.puts == puts && server.archive.size() == 1 && read_user_dictionary(user).at({"ni hao", "你好"}) == 3,
            "idempotent sync reuploaded records, created a new date, or inflated counts");
        save_sync_settings(other, settings);
        result = synchronize_dictionary(other, settings, transport, "2026-10-15");
        check(result.downloaded.added == 1 && read_user_dictionary(other) == read_user_dictionary(user) && server.puts == puts,
            "a new device did not restore the cloud vocabulary without reuploading it");
        save(other, {{{"ni hao", "你好"}, 7}, {{"bei jing", "北京"}, 2}});
        synchronize_dictionary(other, settings, transport, "2026-10-15");
        result = synchronize_dictionary(user, settings, transport, "2026-10-15");
        check(result.downloaded.added == 1 && result.downloaded.updated == 1 && !result.downloaded.backup.empty() &&
            read_user_dictionary(user) == read_user_dictionary(other) && server.archive.at("2026-10-14").at({"ni hao", "你好"}) == 3 &&
            server.archive.at("2026-10-15").at({"ni hao", "你好"}) == 7, "dated bidirectional merge lost history, updates, or backups");
        auto words = read_user_dictionary(user); words[{"shang hai", "上海"}] = 1; save(user, words);
        server.conflict = true;
        result = synchronize_dictionary(user, settings, transport, "2026-10-15");
        check(read_user_dictionary(user).count({"chong qing", "重庆"}) && sync_archive_words(server.archive).count({"shang hai", "上海"}),
            "conditional-write retry lost another device's updates");
        words = read_user_dictionary(user); words[{"tian jin", "天津"}] = 1; save(user, words);
        server.during_put = [&] { auto typed = read_user_dictionary(user); typed[{"xi an", "西安"}] = 2; save(user, typed); };
        synchronize_dictionary(user, settings, transport, "2026-10-15");
        check(read_user_dictionary(user).count({"xi an", "西安"}), "typing during network transfer was locked out or overwritten");
        synchronize_dictionary(user, settings, transport, "2026-10-15");
        check(sync_archive_words(server.archive).count({"xi an", "西安"}), "concurrent local learning was not uploaded on the next pass");
        const auto before = read(user);
        for (const auto reply : {SyncReply{200, "\"1\"", "<html>bad</html>"}, SyncReply{200, {}, header},
            SyncReply{200, "W/\"1\"", header}, SyncReply{401, {}, {}}, SyncReply{500, {}, {}}}) {
            rejects([&] { synchronize_dictionary(user, settings, [&](const auto&, const auto&, const auto&, const auto&) { return reply; }); },
                "bad server response was accepted");
            check(read(user) == before, "failed download changed the local dictionary");
        }
        int conflicts = 0;
        rejects([&] { synchronize_dictionary(user, settings, [&](const auto& method, const auto&, const auto&, const auto&) {
            if (method == "GET") return SyncReply{404, {}, {}};
            ++conflicts; return SyncReply{412, {}, {}};
        }); }, "unending conflicts were not reported");
        check(conflicts == 4 && read(user) == before, "conflict retries were unbounded or changed local data");
        rejects([&] { synchronize_dictionary(user, settings, [&](const auto&, const auto&, const auto&, const auto&) -> SyncReply {
            throw std::runtime_error("offline");
        }); }, "network failure was not reported");
        check(read(user) == before, "network failure erased local words");
        auto disabled = settings; disabled.enabled = false;
        int requests = 0;
        rejects([&] { synchronize_dictionary(user, disabled, [&](const auto&, const auto&, const auto&, const auto&) {
            ++requests; return SyncReply{};
        }); }, "disabled sync was accepted");
        check(requests == 0, "disabled sync accessed the network");
        rejects([&] { synchronize_dictionary(user, settings, [&](const auto&, const auto&, const auto&, const auto&) {
            save_sync_settings(user, disabled); return SyncReply{404, {}, {}};
        }); }, "disabling sync during a download did not stop the upload");
        check(read(user) == before, "changing sync settings altered local words");
        save_sync_settings(user, settings); write(user, "broken dictionary\n");
        const auto prior_puts = server.puts;
        rejects([&] { synchronize_dictionary(user, settings, transport); }, "corrupt local dictionary was uploaded");
        check(server.puts == prior_puts && read(user) == "broken dictionary\n", "corrupt local data was overwritten");
        save_sync_status(user, {"2026-10-15 12:00:00", "2026-10-15 11:50:00", "同步失败"});
        check(read_sync_status(user).success == "2026-10-15 11:50:00", "last successful sync timestamp was lost");
        std::cout << "PASS: " << checks << " dictionary sync checks\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1; }
}
