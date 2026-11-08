#include "pinyin/local_dictionary_sync.hpp"
#include "pinyin/learning_dictionary.hpp"
#include "pinyin/vocabulary_growth.hpp"
#include "test_workspace.hpp"
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
int checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary); if (!(stream << bytes)) throw std::runtime_error("fixture write failed");
}
}
int main() {
    using namespace pinyin;
    try {
        testing::TestWorkspace workspace;
        const auto base = workspace.root / L"base.tsv", user = default_user_path();
        write(base, "基础\tji chu\t90\n电脑\tdian nao\t400\n重载\tchong zai\t20\n");
        const auto main = main_dictionary_path(user);
        check(!local_sync_enabled(user) && synchronize_local_dictionary(base, user, 10000) == LocalSyncOutcome::skipped &&
            !std::filesystem::exists(user.parent_path()), "local sync was not an explicit opt-in");
        set_local_sync_enabled(user, true);
        const UserDictionary original{{{"dian nao", "电脑"}, 8}, {{"xin ci", "新词"}, 3}, {{"zhong zai", "重载"}, 2}};
        { UserStore store(user); store.save(original); }
        LearningDictionary active(base, user); active.refresh();
        const auto before = read_vocabulary_growth(vocabulary_growth_path(user));
        check(synchronize_local_dictionary(base, user, 10000) == LocalSyncOutcome::success && read_user_dictionary(user).empty(),
            "successful sync did not clear the personal queue");
        check(read_dictionary_file(main).lexicon.lookup("xinci").front().text == "新词" &&
            read_dictionary_file(main).lexicon.lookup("diannao").front().weight == 400,
            "sync lost a word or changed an existing sorting weight");
        bool backed_up = false;
        for (const auto& file : std::filesystem::directory_iterator(user.parent_path() / L"backups"))
            if (file.path().filename().wstring().find(L"personal-before-main-sync.") == 0)
                backed_up = backed_up || read_user_dictionary(file.path()) == original;
        check(backed_up, "cleared words and selection counts have no recoverable backup");
        active.refresh();
        check(active.users().empty() && active.lexicon().lookup("xinci").front().text == "新词",
            "running applications lost cleared words instead of loading main.tsv");
        { UserStore store(user); store.save({{{"xin ci", "新词"}, 1}}); }
        check(read_vocabulary_growth(vocabulary_growth_path(user)) == before,
            "using a word after moving it to main counted as a new vocabulary addition");
        check(synchronize_local_dictionary(base, user, 10000 + local_sync_interval_ms - 1) == LocalSyncOutcome::skipped &&
            !read_user_dictionary(user).empty(), "worker ignored its ten-minute interval");
        check(synchronize_local_dictionary(base, user, 10000 + local_sync_interval_ms) == LocalSyncOutcome::success &&
            read_user_dictionary(user).empty(), "worker did not run at the next ten-minute boundary");
        const auto total = dictionary_totals(base, user);
        check(total.texts == 4 && total.readings == 5, "total dictionary count failed to deduplicate texts and readings");
        DictionaryManager supplementary(user.parent_path() / L"dictionaries");
        supplementary.upsert("test", {"辅词", "fu ci", 50});
        supplementary.upsert("test", {"电脑", "dian nao", 100});
        { UserStore store(user); store.save({{{"fu ci", "辅词"}, 2}, {{"ge ren", "个人"}, 1}}); }
        const auto combined = dictionary_totals(base, user);
        check(combined.texts == 6 && combined.readings == 7, "total omitted auxiliary or personal words or counted duplicates");
        supplementary.set_enabled("test", false);
        check(dictionary_totals(base, user).texts == 6, "personal copy of a disabled auxiliary word disappeared from totals");
        const auto personal = read_user_dictionary(user);
        write(main, "bad main dictionary\n");
        check(synchronize_local_dictionary(base, user, 10000 + 2 * local_sync_interval_ms) == LocalSyncOutcome::failure &&
            read_user_dictionary(user) == personal, "failed merge cleared personal words");
        check(read_local_sync_status(user).message.find("失败") != std::string::npos, "failed sync was not visible in status");
        set_local_sync_enabled(user, false);
        check(synchronize_local_dictionary(base, user, 10000 + 3 * local_sync_interval_ms) == LocalSyncOutcome::skipped &&
            read_user_dictionary(user) == personal, "disabled local sync still changed words");
        const auto blocked = workspace.root / L"blocked" / L"words.user.tsv";
        { UserStore store(blocked); store.save(original); }
        write(blocked.parent_path() / L"backups", "occupied");
        set_local_sync_enabled(blocked, true);
        check(synchronize_local_dictionary(base, blocked, 10000) == LocalSyncOutcome::failure &&
            read_user_dictionary(blocked) == original && !std::filesystem::exists(main_dictionary_path(blocked)),
            "backup failure changed main or cleared personal records");
        const auto pinned = workspace.root / L"pinned" / L"words.user.tsv";
        { UserStore store(pinned); store.save(original); }
        set_local_sync_enabled(pinned, true);
        const auto handle = CreateFileW(pinned.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(handle != INVALID_HANDLE_VALUE, "cannot hold personal replacement fixture");
        const auto failed_clear = synchronize_local_dictionary(base, pinned, 10000);
        CloseHandle(handle);
        check(failed_clear == LocalSyncOutcome::failure && read_user_dictionary(pinned) == original &&
            !read_dictionary_file(main_dictionary_path(pinned)).lexicon.lookup("xinci").empty(),
            "failed personal clearing lost the original or the published main copy");
        const auto racing = workspace.root / L"concurrent" / L"words.user.tsv";
        { UserStore store(racing); store.save(original); }
        std::atomic<unsigned> failures{0};
        std::thread sync([&] { try { merge_user_into_main(base, racing, true); } catch (...) { ++failures; } });
        std::thread learner([&] { try {
            UserStore store(racing, 1000); auto words = store.load(); words[{"bing fa ci", "并发词"}] = 1; store.save(words);
        } catch (...) { ++failures; } });
        sync.join(); learner.join();
        const auto saved = read_user_dictionary(racing);
        const auto merged = read_dictionary_file(main_dictionary_path(racing));
        check(!failures && (saved.count({"bing fa ci", "并发词"}) || !merged.lexicon.lookup("bingfaci").empty()),
            "learning concurrently with clearing lost a newly confirmed word");
        std::cout << "PASS: " << checks << " local schedule, backup, clear, growth, totals and concurrency checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
