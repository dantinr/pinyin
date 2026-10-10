#include "pinyin/dictionary_manager.hpp"
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
    std::ifstream input(path, std::ios::binary); return {std::istreambuf_iterator<char>(input), {}};
}
void write(const std::filesystem::path& path, const std::string& bytes) {
    std::ofstream output(path, std::ios::binary);
    if (!(output << bytes)) throw std::runtime_error("cannot write test fixture");
}
template<class Function> void rejects(Function action, const char* message) {
    bool failed = false; try { action(); } catch (const std::exception&) { failed = true; } check(failed, message);
}
bool contains(const pinyin::Lexicon& lexicon, const char* spelling, const char* text) {
    for (const auto& word : lexicon.lookup_composition(spelling)) if (word.text == text) return true;
    return false;
}
}
int main() {
    using namespace pinyin;
    try {
        testing::TestWorkspace workspace;
        const auto root = default_dictionary_directory(), source = workspace.root / L"source.tsv";
        DictionaryManager manager(root);
        check(manager.list().empty() && !std::filesystem::exists(root), "read-only list created a dictionary directory");
        for (const auto* name : {"../outside", "base", "user", "con", "com1", "lpt9", "a/b", "bad.tsv", "Bad", ""})
            rejects([&] { manager.upsert(name, {"测试", "ce shi", 100}); }, "unsafe dictionary name was accepted");
        check(!std::filesystem::exists(root), "name validation wrote files before rejecting an edit");
        write(source, "\xef\xbb\xbf# source: independently reviewed fixture\r\n电脑\tDIAN NAO\t300\r\n"
            "电脑\tdian nao\t400\r\n重载\tchong zai\t100\r\n重载\tzhong zai\t200\r\n");
        const auto inspected = read_dictionary_file(source);
        check(inspected.records == 4 && inspected.lexicon.size() == 3, "validation did not report canonical duplicates");
        check(manager.import_file("computer", source) == 3, "dictionary import lost distinct readings");
        const auto target = root / L"computer.tsv"; const auto first = read(target);
        manager.import_file("computer", source);
        check(read(target) == first && read(target).find("# source: independently reviewed fixture") != std::string::npos,
            "repeated import accumulated entries or lost source notes");
        manager.upsert("computer", {"电脑", "dian nao", 50});
        check(read_dictionary_file(target).lexicon.lookup("diannao").front().weight == 50, "explicit upsert could not lower weight");
        manager.upsert("medical", {"辅库测试词", "fu ku ce shi ci", 300});
        Lexicon base; base.add({"电脑", "dian nao", 1000}); base.add({"基础词", "ji chu ci", 500});
        SupplementaryDictionaries cache(root);
        check(cache.refresh() && !cache.refresh(), "unchanged supplementary dictionaries were rebuilt");
        auto combined = cache.merged(base);
        check(combined.lookup("diannao").front().weight == 1000 && contains(combined, "fukuceshici", "辅库测试词"),
            "main/auxiliary merge summed weights, lost words, or overrode larger main weights");
        manager.set_enabled("medical", false);
        check(cache.refresh() && !contains(cache.merged(base), "fukuceshici", "辅库测试词"), "disable did not unload one dictionary");
        manager.set_enabled("medical", true);
        check(cache.refresh() && contains(cache.merged(base), "fukuceshici", "辅库测试词"), "enable did not restore a dictionary");
        write(source, "坏词\tinvalid\t100\n");
        const auto before = read(target);
        rejects([&] { manager.import_file("computer", source); }, "invalid import was accepted");
        check(read(target) == before, "failed validation damaged the active dictionary");
        write(target, "坏词\tinvalid\t100\n");
        check(cache.refresh() && cache.merged(base).lookup("chongzai").empty() &&
            contains(cache.merged(base), "fukuceshici", "辅库测试词"), "corrupt auxiliary file blocked other dictionaries");
        check(!manager.list().front().error.empty(), "list hid a corrupt dictionary");
        rejects([&] { manager.set_enabled("computer", true); }, "enable accepted a malformed dictionary");
        rejects([&] { manager.upsert("computer", {"测试", "ce shi", 100}); }, "upsert silently discarded a malformed dictionary");
        write(source, "修复词\txiu fu ci\t100\n");
        manager.import_file("computer", source, true);
        check(cache.refresh() && contains(cache.merged(base), "xiufuci", "修复词"), "repair did not reload the active dictionary");
        const auto backup = workspace.root / L"备份.tsv";
        manager.export_file("computer", backup);
        check(read_dictionary_file(backup).lexicon.size() == 1, "export did not preserve the dictionary");
        rejects([&] { manager.export_file("computer", backup); }, "export silently overwrote a backup");
        check(manager.remove("computer", "修复词", "xiu fu ci") && !manager.remove("computer", "修复词", "xiu fu ci") &&
            read_dictionary_file(target).lexicon.size() == 0, "removing the last entry produced an invalid dictionary");
        check(cache.refresh() && cache.merged(base).lookup("xiufuci").empty(), "removed entry survived a reload");
        std::atomic<unsigned> failures{0};
        auto writer = [&](const char* text, const char* reading) {
            try {
                DictionaryManager other(root);
                for (int i = 0; i < 8; ++i) other.upsert("concurrent", {text, reading, static_cast<std::uint64_t>(100 + i)});
            } catch (...) { ++failures; }
        };
        std::thread one(writer, "并发甲", "bing fa jia"), two(writer, "并发乙", "bing fa yi"); one.join(); two.join();
        check(failures == 0 && read_dictionary_file(root / L"concurrent.tsv").lexicon.size() == 2,
            "independent concurrent agents lost each other's additions");
        const auto user = default_user_path(), main_file = workspace.root / L"main.tsv";
        write(main_file, "基础词\tji chu ci\t500\n"); const auto bundled = read(main_file);
        set_ime_learning(user, false);
        LearningDictionary dictionary(main_file, user); dictionary.refresh();
        check(!dictionary.enabled() && dictionary.users().empty() && contains(dictionary.lexicon(), "fukuceshici", "辅库测试词") &&
            read(main_file) == bundled && !std::filesystem::exists(user), "auxiliary words depended on learning or modified main/personal data");
        manager.set_enabled("medical", false); dictionary.refresh();
        check(!contains(dictionary.lexicon(), "fukuceshici", "辅库测试词"), "IME cache ignored a live disable");
        manager.set_enabled("medical", true); dictionary.refresh();
        check(contains(dictionary.lexicon(), "fukuceshici", "辅库测试词"), "IME cache ignored a live enable");
        std::cout << "PASS: " << checks << " supplementary dictionary/transaction/concurrency checks\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
