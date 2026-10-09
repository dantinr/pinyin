#include "pinyin/lexicon.hpp"
#include "pinyin/user_store.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks;
    if (!condition) throw std::runtime_error(message);
}
template <typename Function> void rejects(Function action, const char* message) {
    bool failed = false;
    try { action(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}
bool contains(const std::vector<pinyin::Candidate>& words, const std::string& text) {
    for (const auto& word : words) if (word.text == text) return true;
    return false;
}
std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path, const std::string& data) {
    std::ofstream stream(path, std::ios::binary);
    if (!(stream << data)) throw std::runtime_error("cannot write test fixture");
}
struct TemporaryDirectory {
    std::filesystem::path root;
    TemporaryDirectory() {
        root = std::filesystem::temp_directory_path() /
            (L"private-pinyin-tests-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        if (!std::filesystem::create_directory(root)) throw std::runtime_error("test directory already exists");
    }
    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(root, error); // Only the uniquely created test directory.
    }
};
}

int wmain(int argc, wchar_t* argv[]) {
    try {
        if (argc != 2) throw std::runtime_error("expected fixture dictionary path");
        pinyin::Lexicon base;
        base.load(argv[1]);
        check(base.size() > 80, "sample dictionary failed to load");
        check(base.lookup("nihao").front().text == "你好", "frequency ordering failed");
        check(base.lookup(" NI HAO ").front().text == "你好", "case/space normalization failed");
        check(base.lookup("ni'hao").front().text == "你好", "explicit segmentation failed");
        const auto ambiguous = base.lookup("xian");
        check(contains(ambiguous, "先") && contains(ambiguous, "西安"), "ambiguous syllable paths were lost");
        const auto separated = base.lookup("xi'an");
        check(contains(separated, "西安") && !contains(separated, "先"), "apostrophe did not constrain segmentation");
        check(base.lookup("chongqing").front().text == "重庆", "polyphonic phrase failed");
        check(base.lookup("zhongqing").empty(), "incorrect polyphonic reading was accepted");
        check(base.lookup("yin'hang").front().text == "银行", "phrase-level hang reading failed");
        check(base.lookup("nühai").front().text == "女孩", "umlaut normalization failed");
        check(base.lookup("LÜSE").front().text == "绿色", "uppercase umlaut normalization failed");
        check(base.lookup("niha").front().text == "你好" && base.lookup("niha").front().completed,
              "unfinished final syllable did not offer a completed word");
        check(base.lookup("zzzz").empty(), "unmatched query should be empty");
        check(base.lookup("xian", {}, 1).size() == 1, "candidate limit failed");
        check(base.lookup("nihao", {}, 0).empty(), "zero limit failed");
        rejects([&] { base.lookup("nihao1"); }, "tone digits were accepted");
        rejects([&] { base.lookup("'nihao"); }, "leading apostrophe was accepted");
        rejects([&] { base.lookup("ni''hao"); }, "consecutive apostrophes were accepted");
        rejects([&] { base.lookup("nihao'"); }, "trailing apostrophe was accepted");
        check(base.lookup(std::string(300, 'a')).empty(), "long valid input was rejected");
        rejects([&] { base.lookup("你好"); }, "non-pinyin input was accepted");
        rejects([&] { base.add({"错误", "nihao", 10}); }, "unseparated dictionary pronunciation was accepted");
        rejects([&] { base.add({"错误", "zzz", 10}); }, "invalid dictionary syllable was accepted");
        rejects([&] { base.add({std::string("\xc0\xaf"), "ni", 10}); }, "overlong UTF-8 was accepted");
        rejects([&] { base.add({"错误\t词", "cuo", 10}); }, "control character was accepted");
        const auto size = base.size();
        base.add({"你好", "NI HAO", 20000});
        check(base.size() == size && base.lookup("nihao").front().weight == 20000,
              "duplicate merge should keep larger weight");
        pinyin::UserDictionary users;
        const auto less_frequent = base.lookup("beijing")[1];
        pinyin::learn(users, less_frequent);
        check(base.lookup("beijing", users).front().text == "背景", "a personal choice did not take effect immediately");
        for (int i = 1; i < 20; ++i) pinyin::learn(users, less_frequent);
        check(base.lookup("beijing", users).front().text == "背景", "personal ranking did not adapt");
        check(base.lookup("beijing").front().text == "北京", "stateless ranking was affected by learning");
        auto capped = users;
        capped[{"bei jing", "背景"}] = 1000000000;
        pinyin::learn(capped, less_frequent);
        check(capped.at({"bei jing", "背景"}) == 1000000000, "counter cap failed");
        pinyin::Lexicon repeated;
        repeated.add({"重", "zhong", 10});
        repeated.add({"重", "chong", 20});
        check(repeated.size() == 2, "distinct readings were merged");
        pinyin::Lexicon personal;
        personal.add({"常", "chang", 10000}); personal.add({"昶", "chang", 1}); personal.add({"敞", "chang", 1});
        pinyin::UserDictionary preferences{{{"chang", "昶"}, 1}};
        check(personal.lookup("chang", preferences).front().text == "昶", "a rare personal word stayed behind the base frequency");
        preferences[{"chang", "昶"}] = 100; preferences[{"chang", "敞"}] = 1000;
        check(personal.lookup("chang", preferences).front().text == "敞", "personal ranking still capped the number of uses at 100");

        // A deep learned phrase must also be queryable without recursive trie
        // traversal exhausting the native thread's stack.
        pinyin::Candidate long_word; std::string long_query;
        for (int i = 0; i < 4096; ++i) {
            long_word.text += "汉"; long_query += "han";
            if (i) long_word.pronunciation += ' ';
            long_word.pronunciation += "han";
        }
        pinyin::Lexicon long_lexicon; long_lexicon.add({long_word.text, long_word.pronunciation, 1});
        check(long_lexicon.lookup(long_query).front().text == long_word.text &&
            long_lexicon.lookup_composition(long_query).front().pronunciation == long_word.pronunciation,
            "long custom phrase failed lookup or prefix matching");

        TemporaryDirectory temporary;
        const auto broken = temporary.root / L"broken.tsv";
        write(broken, "你好\tni hao\t10\n错误\tzzz\t10\n");
        rejects([&] { base.load(broken); }, "malformed dictionary did not fail");
        check(base.size() == size && base.lookup("nihao").front().weight == 20000,
              "failed load changed the live dictionary");
        write(broken, "\xef\xbb\xbf# fixture\r\n你好\tni hao\t10\r\n");
        pinyin::Lexicon bom;
        bom.load(broken);
        check(bom.lookup("nihao").front().text == "你好", "BOM/CRLF handling failed");
        const auto user_path = temporary.root / L"中文路径" / L"words.user.tsv";
        {
            pinyin::UserStore store(user_path);
            check(store.load().empty(), "missing user file should be empty");
            rejects([&] { pinyin::UserStore competing(user_path); }, "exclusive session lock failed");
            store.save(users);
            check(store.load() == users, "user dictionary round trip failed");
            const auto old_bytes = read(user_path);
            auto invalid = users;
            invalid[{"not a syllable", "无效"}] = 1;
            rejects([&] { store.save(invalid); }, "invalid user dictionary was saved");
            check(read(user_path) == old_bytes, "failed save changed persisted data");
            pinyin::Candidate custom;
            custom.text = "隐私输入法";
            custom.pronunciation = "yin si shu ru fa";
            pinyin::learn(users, custom);
            pinyin::learn(users, long_word);
            store.save(users);
            check(store.load() == users, "replacement save lost records");
            auto combined = base;
            for (const auto& item : store.load()) combined.add({item.first.second, item.first.first, 1});
            check(combined.lookup("yinsishurufa", users).front().text == "隐私输入法", "custom word failed to query");
            store.save({});
            check(store.load().empty(), "clear user dictionary failed");
            write(user_path, "背景\tbei jing\tinvalid\n");
            rejects([&] { store.load(); }, "corrupt user count was accepted");
            check(read(user_path) == "背景\tbei jing\tinvalid\n", "load overwrote corrupt data");
            write(user_path, "背景\tbei jing\t1\n背景\tbei jing\t2\n");
            rejects([&] { store.load(); }, "duplicate user record was accepted");
        }
        { pinyin::UserStore reopened(user_path); } // Lock must be released at destruction.
        for (const auto& item : std::filesystem::directory_iterator(user_path.parent_path()))
            check(item.path().filename().wstring().find(L".tmp.") == std::wstring::npos, "temporary dictionary leaked");
        std::cout << "PASS: " << checks << " checks\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        return 1;
    }
}
