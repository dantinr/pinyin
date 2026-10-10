#include "pinyin/learning_dictionary.hpp"
#include "pinyin/ime_settings.hpp"
#include "test_workspace.hpp"
#include <fstream>
#include <iostream>

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    if (!(output << bytes)) throw std::runtime_error("cannot write migration fixture");
}
std::string read(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}
}
int main() {
    using namespace pinyin;
    try {
        testing::TestWorkspace workspace;
        const auto user = default_user_path();
        check(user == workspace.root / L"profile" / L"PrivatePinyin" / L"words.user.tsv",
            "default store still uses virtualized AppData");
        check(!std::filesystem::exists(user.parent_path()), "fresh lookup created user storage");
        const auto appdata = workspace.root / L"appdata";
        const auto old = appdata / L"PrivatePinyin";
        const auto package = appdata / L"Packages" / L"Example.App_123" / L"LocalCache" / L"Local" / L"PrivatePinyin";
        const auto broken = appdata / L"Packages" / L"Other.App_456" / L"LocalCache" / L"Local" / L"PrivatePinyin";
        const std::string original = "课堂\tke tang\t2\n专员\tzhuan yuan\t3\n";
        const std::string private_copy = "课堂\tke tang\t1\n专员\tzhuan yuan\t5\n剪映\tjian ying\t1\n";
        write(old / L"words.user.tsv", original);
        write(package / L"words.user.tsv", private_copy);
        write(broken / L"words.user.tsv", "坏词\tinvalid\t9\n");
        write(old / L"unrelated.tsv", "无关\twu guan\t100\n");
        write(old / L"words.user.tsv.punctuation.disabled", {});
        write(old / L"words.user.tsv.ime-learning.disabled", {});
        write(old / L"dictionaries" / L"daily.tsv", "课程\tke cheng\t5\n");
        write(old / L"dictionaries" / L"daily.tsv.disabled", {});
        write(package / L"dictionaries" / L"daily.tsv", "课程\tke cheng\t20\n课本\tke ben\t7\n");
        write(package / L"dictionaries" / L"private.tsv", "隐私\tyin si\t8\n");
        write(package / L"dictionaries" / L"private.tsv.disabled", {});
        check(default_user_path() == user, "migration changed the shared destination");
        const auto users = read_user_dictionary(user);
        check(users.size() == 3 && users.at({"ke tang", "课堂"}) == 2 &&
            users.at({"zhuan yuan", "专员"}) == 5 && users.at({"jian ying", "剪映"}) == 1,
            "migration missed a package/shared word or summed duplicate counts");
        check(read(old / L"words.user.tsv") == original && read(package / L"words.user.tsv") == private_copy,
            "migration modified old personal stores");
        check(!read(user.parent_path() / L"migration" / L"errors.txt").empty(),
            "invalid legacy data was silently marked as migrated");
        bool preserved_snapshot = false;
        for (const auto& item : std::filesystem::directory_iterator(user.parent_path() / L"migration"))
            if (item.path().extension() == L".tsv" && read(item.path()) == original) preserved_snapshot = true;
        check(preserved_snapshot, "original legacy snapshot was not preserved");
        const auto settings = read_ime_settings(user);
        check(!settings.learning && !settings.chinese_punctuation && settings.automatic_english,
            "migration lost the current host's settings");
        DictionaryManager manager(default_dictionary_directory());
        const auto daily = read_dictionary_file(manager.root() / L"daily.tsv").lexicon;
        check(daily.size() == 2 && daily.lookup("kecheng").front().weight == 20,
            "migration lost supplementary entries or inflated weights");
        for (const auto& info : manager.list()) check(!info.enabled, "migration lost a supplementary disable marker");
        set_ime_learning(user, true); set_chinese_punctuation(user, true);
        const auto base = workspace.root / L"base.tsv";
        write(base, "课\tke\t20\n堂\ttang\t20\n");
        LearningDictionary observer(base, default_user_path()); observer.refresh();
        const auto candidates = observer.lexicon().lookup_composition("ketang", observer.users());
        check(!candidates.empty() && candidates.front().text == "课堂",
            "migrated classroom was not available as a whole-word candidate");
        check(read_user_dictionary(user) == users && read_ime_settings(user).learning &&
            read_ime_settings(user).chinese_punctuation, "repeated migration inflated counts or reverted new settings");
        write(broken / L"words.user.tsv", "额外\te wai\t3\n");
        default_user_path();
        check(read_user_dictionary(user).at({"e wai", "额外"}) == 3 &&
            !std::filesystem::exists(user.parent_path() / L"migration" / L"errors.txt"),
            "migration did not retry a repaired legacy file");
        { UserStore store(user); store.save({}); }
        default_user_path();
        check(read_user_dictionary(user).empty(), "migration resurrected cleared personal words");
        manager.remove("daily", "课程", "ke cheng"); manager.set_enabled("daily", true);
        default_user_path();
        check(read_dictionary_file(manager.root() / L"daily.tsv").lexicon.size() == 1 &&
            manager.list().front().enabled, "migration reverted later supplementary edits");
        // A pre-existing shared store and its current settings take precedence.
        const auto existing = workspace.root / L"existing" / L"words.user.tsv";
        { UserStore store(existing); store.save({{{"xin ci", "新词"}, 7}}); }
        migrate_legacy_user_data(existing, appdata);
        check(read_user_dictionary(existing).at({"xin ci", "新词"}) == 7 &&
            read_ime_settings(existing).learning && read_ime_settings(existing).chinese_punctuation,
            "migration overwrote an existing shared store's words or settings");
        // Host AppData may differ, but the shared root must remain stable.
        SetEnvironmentVariableW(L"LOCALAPPDATA", (workspace.root / L"another-appdata").c_str());
        check(default_user_path() == user, "host-specific AppData changed the active personal dictionary");
        std::cout << "PASS: " << checks << " shared storage/migration checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
