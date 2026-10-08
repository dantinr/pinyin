#include "pinyin/learning_dictionary.hpp"
#include "test_workspace.hpp"
#include <algorithm>
#include <atomic>
#include <fstream>
#include <iostream>
#include <thread>

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
std::string read(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path, const std::string& data) {
    std::ofstream stream(path, std::ios::binary);
    if (!(stream << data)) throw std::runtime_error("cannot write fixture");
}
void type(pinyin::InputSession& input, const pinyin::LearningDictionary& dictionary, const char* spelling) {
    while (*spelling) input.handle(pinyin::InputKey::letter, *spelling++, dictionary.lexicon(), dictionary.users());
}
}
int main() {
    using namespace pinyin;
    try {
        testing::TestWorkspace workspace;
        const auto base = workspace.root / L"base.tsv", path = default_user_path();
        write(base, "如\tru\t20\n入\tru\t10\n何\the\t20\n和\the\t10\n");
        check(ime_learning_enabled(path) && !std::filesystem::exists(path.parent_path()),
            "fresh settings did not default to learning or created storage during lookup");
        check(!ime_learning_enabled({}), "missing user path enabled local learning");
        LearningDictionary dictionary(base, path);
        dictionary.refresh();
        check(dictionary.enabled() && dictionary.available() && dictionary.users().empty() && !std::filesystem::exists(path),
            "default local learning failed or invented word records");
        const InputResult confirmed{InputAction::commit, "如何", "ru he"};
        InputSession input; type(input, dictionary, "ruhe");
        const auto prefix = std::find_if(input.candidates().begin(), input.candidates().end(),
            [](const auto& word) { return word.text == "如"; });
        if (prefix == input.candidates().end()) throw std::runtime_error("missing partial selection fixture");
        auto result = input.select(static_cast<std::size_t>(prefix - input.candidates().begin()),
            dictionary.lexicon(), dictionary.users());
        check(result.action == InputAction::update && input.preedit() == "如he", "partial selection regression");
        check(!dictionary.remember(result) && !std::filesystem::exists(path), "partial word was persisted");
        result = input.handle(InputKey::space, 0, dictionary.lexicon(), dictionary.users());
        check(result.text == "如何" && dictionary.remember(result), "complete selected phrase was not learned");
        check(dictionary.lexicon().lookup("ruhe", dictionary.users()).front().text == "如何", "learned phrase was not immediately queryable");
        check(read(path).find("如何\tru he\t1") != std::string::npos && read(path).find("ruhe") == std::string::npos,
            "user record contains raw input or incorrect pronunciation/count");
        LearningDictionary restarted(base, path); restarted.refresh();
        type(input, restarted, "ruhe");
        check(input.candidates().front().text == "如何", "restart did not promote whole-word matching");
        result = input.handle(InputKey::space, 0, restarted.lexicon(), restarted.users());
        check(restarted.remember(result) && restarted.users().at({"ru he", "如何"}) == 2, "repeated use duplicated a word instead of increasing its count");
        const auto saved = read(path);
        for (const auto& ignored : {
            InputResult{InputAction::commit, "ruhe", {}}, InputResult{InputAction::cancel, "如何", "ru he"},
            InputResult{InputAction::update, "如", "ru"}, InputResult{InputAction::commit, "如he", "ru he"},
            InputResult{InputAction::commit, "如何", "ru"}, InputResult{InputAction::commit, "如何", "invalid"},
            InputResult{InputAction::commit, "如如何何如如何何如如何何如如何何如", "ru"}})
            check(!dictionary.remember(ignored) && read(path) == saved, "unconfirmed/non-Chinese/malformed/long text was learned");

        // Two independent caches use separate kernel file handles, like two apps.
        std::atomic<unsigned> failures{0};
        auto writer = [&](const char* text) {
            try {
                LearningDictionary client(base, path);
                for (int i = 0; i < 8; ++i)
                    if (!client.remember({InputAction::commit, text, "ru he"})) ++failures;
            } catch (...) { ++failures; }
        };
        std::thread first(writer, "入何"), second(writer, "入和"); first.join(); second.join();
        UserDictionary merged;
        { UserStore store(path); merged = store.load(); }
        check(failures == 0 && merged.at({"ru he", "入何"}) == 8 && merged.at({"ru he", "入和"}) == 8 &&
            merged.at({"ru he", "如何"}) == 2, "concurrent learning lost an application's updates");
        dictionary.refresh(); check(dictionary.users() == merged, "next composition did not reload another application's words");
        set_ime_learning(path, false); dictionary.refresh();
        check(!dictionary.enabled() && dictionary.users().empty() && dictionary.lexicon().lookup("ruhe").empty(),
            "disabling learning kept personal words in the active lexicon");
        check(!dictionary.remember(confirmed) && read(path).find("入何") != std::string::npos, "turning learning off deleted existing records");
        LearningDictionary disabled_restart(base, path); disabled_restart.refresh();
        check(!disabled_restart.enabled() && disabled_restart.users().empty() && !disabled_restart.remember(confirmed),
            "restart forgot an explicit opt-out");
        set_ime_learning(path, true);
        dictionary.refresh();
        check(!std::filesystem::exists(ime_learning_flag(path)) && dictionary.enabled() && dictionary.users() == merged,
            "enabling learning did not restore the default or reload existing words");
        {
            UserStore held(path); const auto previous = read(path);
            check(!dictionary.remember(confirmed) && read(path) == previous, "busy dictionary was overwritten");
        }
        write(path, "如何\tru he\tinvalid\n"); const auto corrupt = read(path);
        dictionary.refresh();
        check(!dictionary.available() && dictionary.lexicon().lookup("ruhe").empty(), "corrupt user data blocked the base dictionary");
        check(!dictionary.remember(confirmed) && read(path) == corrupt, "learning replaced corrupt personal data");
        { UserStore store(path); store.save({}); }
        dictionary.refresh();
        check(dictionary.remember(confirmed), "learning did not recover after user data repair");
        std::cout << "PASS: " << checks << " local learning/privacy/concurrency checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
