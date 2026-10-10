#include "pinyin/user_store.hpp"
#include "pinyin/learning_dictionary.hpp"
#include "test_workspace.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
int checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}};
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary);
    if (!(file << bytes)) throw std::runtime_error("cannot write test fixture");
}
template<class F> void rejects(F action, const char* message) {
    bool failed = false; try { action(); } catch (const std::exception&) { failed = true; } check(failed, message);
}
}
int main() {
    using namespace pinyin;
    try {
        testing::TestWorkspace workspace;
        const auto user = default_user_path(), output = workspace.root / L"个人 导出.tsv";
        const auto source = workspace.root / L"合并 词库.tsv";
        check(export_user_dictionary(user, output) == 0 && read_user_dictionary(output).empty() &&
            !std::filesystem::exists(user.parent_path()), "empty export created live user storage");
        rejects([&] { export_user_dictionary(user, output); }, "export overwrote an existing file without consent");
        check(!std::filesystem::exists(output.wstring() + L".lock"), "export created a session lock beside the destination");
        write(source, "\xef\xbb\xbf# private-pinyin user dictionary v1: text<TAB>pinyin<TAB>selection_count\r\n"
            "电脑\tDIAN NAO\t3\r\n电脑\tdian nao\t5\r\n重载\tchong zai\t2\r\n重载\tzhong zai\t7\r\n");
        const auto inspected = read_personal_dictionary_file(source);
        check(inspected.records == 4 && inspected.users.size() == 3 && inspected.users.at({"dian nao", "电脑"}) == 5,
            "duplicate canonical imports or distinct readings were lost");
        const auto original_source = read(source);
        auto result = merge_user_dictionary(user, source);
        check(result.added == 3 && result.updated == 0 && result.total == 3 && result.backup.empty(),
            "new personal dictionary import had wrong counts");
        check(read(source) == original_source, "import modified its source file");
        check(export_user_dictionary(user, output, true) == 3 && read_user_dictionary(output) == inspected.users,
            "export did not preserve selection counts or different readings");
        rejects([&] { export_user_dictionary(user, user, true); }, "export could replace its own live source");
        const auto alias = workspace.root / L"hardlink.tsv";
        check(CreateHardLinkW(alias.c_str(), user.c_str(), nullptr) != 0, "cannot create alias fixture");
        rejects([&] { export_user_dictionary(user, alias, true); }, "export did not detect a hard-link source alias");
        const auto before = read(user);
        write(source, "电脑\tdian nao\t2\n重载\tchong zai\t8\n合并测试词\the bing ce shi ci\t4\n");
        set_ime_learning(user, false);
        result = merge_user_dictionary(user, source);
        check(result.imported == 3 && result.added == 1 && result.updated == 1 && result.unchanged == 1 && result.total == 4,
            "merge did not take maximum selection counts");
        check(!result.backup.empty() && read(result.backup) == before, "automatic backup did not preserve the exact previous file");
        check(!ime_learning_enabled(user), "explicit merge turned learning back on");
        const auto merged = read(user);
        const auto saved_backup = result.backup;
        result = merge_user_dictionary(user, source);
        check(result.added == 0 && result.updated == 0 && result.unchanged == 3 && result.backup.empty() && read(user) == merged,
            "repeated import accumulated counts, rewrote data, or created unnecessary backups");
        check(read_user_dictionary(user).at({"dian nao", "电脑"}) == 5 && read_user_dictionary(user).at({"chong zai", "重载"}) == 8,
            "merge lowered an existing usage count");
        result = merge_user_dictionary(user, user);
        check(result.unchanged == 4 && result.backup.empty(), "self merge was not idempotent");
        for (const auto& bytes : {std::string("坏词\tinvalid\t1\n"), std::string("测试\tce shi\t0\n"),
            std::string("测试\tce shi\t1000000001\n"), std::string("测试\tce shi\t1\textra\n"),
            std::string("\xff\tce shi\t1\n"),
            std::string("# private-pinyin supplementary dictionary v1\n测试\tce shi\t1000\n"),
            std::string("# Private Pinyin independently curated starter lexicon, 2026-10-08.\n测试\tce shi\t1000\n")}) {
            write(source, bytes);
            rejects([&] { merge_user_dictionary(user, source); }, "invalid or ordinary weighted dictionary was imported as personal usage");
            check(read(user) == merged && read(saved_backup) == before, "rejected merge modified existing data");
        }
        const auto absent = workspace.root / L"absent.tsv", unused = workspace.root / L"unused" / L"user.tsv";
        rejects([&] { merge_user_dictionary(unused, absent); }, "missing source was silently treated as empty");
        check(!std::filesystem::exists(unused.parent_path()), "failed input validation created personal storage");
        write(source, "合并新增词\the bing xin zeng ci\t9\n");
        write(user, "损坏\tinvalid\t1\n");
        rejects([&] { merge_user_dictionary(user, source); }, "merge silently discarded a corrupt live dictionary");
        check(read(user) == "损坏\tinvalid\t1\n", "merge overwrote a corrupt live dictionary");
        write(user, merged);
        {
            UserStore held(user);
            rejects([&] { merge_user_dictionary(user, source); }, "merge ignored an active writer lock");
            check(read(user) == merged, "lock timeout modified personal data");
            check(export_user_dictionary(user, output, true) == 4, "read-only export waited for the writer lock");
        }
        const auto failure_user = workspace.root / L"backup-failure" / L"user.tsv";
        std::filesystem::create_directories(failure_user.parent_path());
        write(failure_user, merged); write(failure_user.parent_path() / L"backups", "occupied");
        rejects([&] { merge_user_dictionary(failure_user, source); }, "merge continued after a backup failure");
        check(read(failure_user) == merged, "backup failure changed the live file");
        const auto pinned = CreateFileW(user.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(pinned != INVALID_HANDLE_VALUE, "cannot pin published file for failure test");
        try {
            rejects([&] { merge_user_dictionary(user, source); }, "merge bypassed a rejected atomic replacement");
            check(read(user) == merged, "failed atomic replacement damaged live data");
        } catch (...) { CloseHandle(pinned); throw; }
        CloseHandle(pinned);
        std::atomic<unsigned> failures{0};
        std::thread importer([&] {
            try { for (int i = 0; i < 8; ++i) merge_user_dictionary(user, source); } catch (...) { ++failures; }
        });
        std::thread learner([&] {
            try {
                for (int i = 0; i < 8; ++i) {
                    UserStore store(user, 1000); auto words = store.load(); ++words[{"bing fa xue xi ci", "并发学习词"}]; store.save(words);
                }
            } catch (...) { ++failures; }
        });
        importer.join(); learner.join(); const auto final = read_user_dictionary(user);
        check(failures == 0 && final.at({"bing fa xue xi ci", "并发学习词"}) == 8 &&
            final.at({"he bing xin zeng ci", "合并新增词"}) == 9 && final.at({"dian nao", "电脑"}) == 5,
            "concurrent imports and learning overwrote unrelated records or accumulated imported usage");
        set_ime_learning(user, true);
        const auto base = workspace.root / L"base.tsv"; write(base, "基础词\tji chu ci\t100\n");
        LearningDictionary active(base, user); active.refresh();
        check(active.users() == final && active.lexicon().lookup("hebingxinzengci", final).front().text == "合并新增词",
            "merged personal records were not usable by the IME reader");
        for (const auto& file : std::filesystem::recursive_directory_iterator(workspace.root))
            check(file.path().filename().wstring().find(L".tmp.") == std::wstring::npos, "failed operation leaked a temporary file");
        std::cout << "PASS: " << checks << " personal dictionary export/merge/backup/concurrency checks\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
