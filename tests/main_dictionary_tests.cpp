#include "pinyin/learning_dictionary.hpp"
#include "test_workspace.hpp"
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>

namespace {
int checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream file(path, std::ios::binary);
    if (!(file << bytes)) throw std::runtime_error("cannot write test dictionary");
}
std::string read(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
template<class Action> void rejects(Action action, const char* message) {
    bool failed = false;
    try { action(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}
}
int main() {
    using namespace pinyin;
    try {
        testing::TestWorkspace workspace;
        const auto base = workspace.root / L"base.tsv", user = default_user_path();
        const auto main = main_dictionary_path(user);
        write(base, "# original main dictionary\n基础词\tji chu ci\t90\n电脑\tdian nao\t400\n重载\tchong zai\t20\n");
        const auto original_base = read(base);
        const UserDictionary words{{{"dian nao", "电脑"}, 8}, {{"qian ru", "嵌入"}, 1000000000},
            {{"chong zai", "重载"}, 3}, {{"zhong zai", "重载"}, 2}};
        { UserStore store(user); store.save(words); }
        const auto original_user = read(user);
        set_ime_learning(user, false);
        LearningDictionary active(base, user); active.refresh();
        check(!std::filesystem::exists(main) && active.lexicon().lookup("qianru").empty(),
            "ordinary lookup created or invented a user main dictionary");
        const auto result = merge_user_into_main(base, user);
        check(result.imported == 4 && result.added == 2 && result.unchanged == 2 && result.total == 5,
            "main dictionary merge counts are incorrect");
        check(result.path == main && read(result.backup) == original_base,
            "first merge did not back up the original bundled main dictionary");
        check(read(base) == original_base && read(user) == original_user && !ime_learning_enabled(user),
            "merging changed bundled files, user selection counts or the privacy switch");
        const auto merged = read_dictionary_file(main);
        check(merged.lexicon.lookup("diannao").front().weight == 400 &&
            merged.lexicon.lookup("qianru").front().weight == 12000,
            "merge confused usage counts with weights or changed existing weights");
        check(merged.lexicon.lookup("chongzai").front().pronunciation == "chong zai" &&
            merged.lexicon.lookup("zhongzai").front().pronunciation == "zhong zai",
            "main merge lost a distinct pronunciation");
        rejects([&] { read_personal_dictionary_file(main); }, "sorting weights were accepted as personal usage counts");
        const auto first_main = read(main);
        const auto repeated = merge_user_into_main(base, user);
        check(repeated.added == 0 && repeated.unchanged == 4 && repeated.backup.empty() && read(main) == first_main,
            "repeated main merge rewrote data, added duplicates or created a backup");
        active.refresh();
        check(!active.enabled() && active.users().empty() && active.lexicon().lookup("qianru").front().text == "嵌入",
            "running input method did not refresh the main dictionary while learning was disabled");
        {
            UserStore store(user); auto updated = words; updated[{"he bing xin ci", "合并新词"}] = 1; store.save(updated);
        }
        const auto added = merge_user_into_main(base, user);
        check(added.added == 1 && added.unchanged == 4 && read(added.backup) == first_main,
            "subsequent merge did not preserve the previous main dictionary in its backup");
        active.refresh();
        check(!active.lexicon().lookup("hebingxinci").empty(), "running main cache missed another merge");
        const auto valid_main = read(main);
        write(main, "损坏\tinvalid\t1\n"); active.refresh();
        check(!active.lexicon().lookup("qianru").empty(), "a damaged main dictionary cleared the last valid cache");
        rejects([&] { merge_user_into_main(base, user); }, "merge overwrote a damaged existing main dictionary");
        check(read(main) == "损坏\tinvalid\t1\n", "failed merge replaced the damaged original");
        write(main, valid_main); active.refresh();
        const auto before_invalid = read(main);
        write(user, "错误\tinvalid\t1\n");
        rejects([&] { merge_user_into_main(base, user); }, "invalid personal input was accepted");
        check(read(main) == before_invalid, "invalid personal input modified the main dictionary");
        { UserStore store(user); store.save(words); }
        {
            UserStore held(main);
            rejects([&] { merge_user_into_main(base, user); }, "another main merge bypassed the writer lock");
        }
        const auto blocked_user = workspace.root / L"blocked" / L"words.user.tsv";
        { UserStore store(blocked_user); store.save(words); }
        write(blocked_user.parent_path() / L"backups", "not a directory");
        rejects([&] { merge_user_into_main(base, blocked_user); }, "merge continued after backup creation failed");
        check(!std::filesystem::exists(main_dictionary_path(blocked_user)), "failed backup published a main dictionary");
        const auto alias_user = workspace.root / L"alias" / L"words.user.tsv";
        { UserStore store(alias_user); store.save(words); }
        check(CreateHardLinkW(main_dictionary_path(alias_user).c_str(), alias_user.c_str(), nullptr) != 0,
            "cannot create main dictionary alias fixture");
        rejects([&] { merge_user_into_main(base, alias_user); }, "main merge overwrote an aliased personal dictionary");
        const auto concurrent_root = workspace.root / L"concurrent";
        const auto first_user = concurrent_root / L"first.user.tsv", second_user = concurrent_root / L"second.user.tsv";
        { UserStore store(first_user); store.save({{{"bing fa jia", "并发甲"}, 1}}); }
        { UserStore store(second_user); store.save({{{"bing fa yi", "并发乙"}, 1}}); }
        std::atomic<unsigned> failures{0};
        auto merge = [&](const auto& source) { try { merge_user_into_main(base, source); } catch (...) { ++failures; } };
        std::thread first(merge, first_user), second(merge, second_user); first.join(); second.join();
        const auto concurrent = read_dictionary_file(main_dictionary_path(first_user));
        check(!failures && concurrent.lexicon.size() == 5 && !concurrent.lexicon.lookup("bingfajia").empty() &&
            !concurrent.lexicon.lookup("bingfayi").empty(), "concurrent main merges lost an application's additions");
        std::filesystem::remove(main); active.refresh();
        check(active.lexicon().lookup("qianru").empty() && !active.lexicon().lookup("diannao").empty(),
            "removing the custom main dictionary did not restore the bundled dictionary");
        std::cout << "PASS: " << checks << " main dictionary merge/backup/reload/concurrency checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
