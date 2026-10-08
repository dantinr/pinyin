#include "pinyin/learning_dictionary.hpp"
#include "test_workspace.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <set>

namespace {
int checks = 0;
void check(bool condition, const char* message) {
    ++checks; if (!condition) throw std::runtime_error(message);
}
bool contains(const std::vector<pinyin::Candidate>& words, const std::string& text) {
    return std::any_of(words.begin(), words.end(), [&](const auto& word) { return word.text == text; });
}
std::size_t index_of(const pinyin::InputSession& input, const std::string& text) {
    for (std::size_t i = 0; i < input.candidates().size(); ++i)
        if (input.candidates()[i].text == text) return i;
    throw std::runtime_error("missing candidate: " + text);
}
void type(pinyin::InputSession& input, const pinyin::Lexicon& lexicon,
          const std::string& text, const pinyin::UserDictionary& users = {}) {
    for (const auto ch : text) input.handle(ch == '\'' ? pinyin::InputKey::separator : pinyin::InputKey::letter, ch, lexicon, users);
}
}
int wmain(int argc, wchar_t* argv[]) {
    using namespace pinyin;
    try {
        if (argc != 2) throw std::runtime_error("expected dictionary");
        Lexicon lexicon; lexicon.load(argv[1]);
        const std::string spelling = "wozaibeijingshangban", chinese = "我在北京上班", reading = "wo zai bei jing shang ban";
        check(lexicon.lookup(spelling).empty(), "sentence fixture was hardcoded into the word dictionary");
        const auto words = lexicon.decode(spelling);
        check(words.size() == 5 && words.front().text == chinese && words.front().synthesized,
            "offline sentence ranking failed");
        check(words.front().pronunciation == reading && words.front().input_end == spelling.size(),
            "sentence commit metadata lost a syllable");
        check(lexicon.decode(" WO ZAI BEI JING SHANG BAN ").front().text == chinese, "normalization failed");
        check(lexicon.decode("wozaichongqingshangban").front().text == "我在重庆上班", "phrase reading failed");
        check(lexicon.decode("woxihuanyinyue").front().text == "我喜欢音乐", "multi-character word ranking failed");
        check(!contains(lexicon.decode("wozaizhongqingshangban"), "我在重庆上班"), "invented Chongqing reading");
        check(!contains(lexicon.decode("yin'xing"), "银行"), "invented bank reading");
        const auto direct = lexicon.decode("nihao");
        check(direct.size() >= 2 && direct[0].text == "你好" && direct[1].text == "拟好" && !direct[0].synthesized,
            "generated candidates displaced direct words");
        check(!contains(lexicon.decode("xi'an"), "先") && contains(lexicon.decode("xian"), "先"), "apostrophe boundary failed");
        std::set<std::string> unique;
        for (const auto& word : words) {
            check(unique.insert(word.text).second, "duplicate sentence candidate");
            check(word.input_end == spelling.size() && !word.pronunciation.empty(), "incomplete whole-sentence candidate");
        }
        check(lexicon.decode(spelling, {}, 1).size() == 1 && lexicon.decode(spelling, {}, 0).empty(), "result limit failed");
        check(lexicon.decode("wozaibeijingshangb").empty() && lexicon.decode("wozz").empty(), "unfinished input was completed");
        for (const auto& invalid : {"wo1", "'wo", "wo''zai", "wo'"}) {
            bool rejected = false;
            try { lexicon.decode(invalid); } catch (const std::invalid_argument&) { rejected = true; }
            check(rejected, "invalid input accepted");
        }
        UserDictionary users; users[{"bei jing", "背景"}] = 20;
        check(lexicon.decode(spelling, users).front().text == "我在背景上班", "local preference not used within a sentence");
        check(lexicon.decode(spelling).front().text == chinese, "local preference changed the base dictionary");
        auto custom = lexicon; custom.add({"北景", "bei jing", 1});
        UserDictionary custom_users; custom_users[{"bei jing", "北景"}] = 3;
        check(custom.decode(spelling, custom_users).front().text == "我在北景上班", "confirmed custom word not reused inside a sentence");

        InputSession input; type(input, lexicon, spelling);
        check(input.candidates().front().text == chinese, "sentence candidate missing from IME input session");
        auto result = input.handle(InputKey::space, 0, lexicon);
        check(result.action == InputAction::commit && result.text == chinese && result.pronunciation == reading && input.empty(),
            "whole-sentence selection failed");
        type(input, lexicon, spelling); input.select(index_of(input, "我"), lexicon);
        check(input.preedit() == "我zaibeijingshangban" && input.candidates().front().text == "在北京上班",
            "prefix correction lost the rest of the sentence");
        input.select(index_of(input, "在"), lexicon); input.select(index_of(input, "背景"), lexicon);
        result = input.handle(InputKey::space, 0, lexicon);
        check(result.text == "我在背景上班" && result.pronunciation == reading, "manual clause correction failed");
        type(input, lexicon, spelling); input.select(index_of(input, "我"), lexicon);
        input.handle(InputKey::home, 0, lexicon);
        check(input.raw() == spelling && input.confirmed_text().empty() && input.cursor() == 0, "Home failed to reopen selected segments");
        result = input.handle(InputKey::escape, 0, lexicon);
        check(result.action == InputAction::cancel && result.pronunciation.empty() && input.empty(), "cancelled sentence became learnable");
        type(input, lexicon, spelling); result = input.handle(InputKey::enter, 0, lexicon);
        check(result.text == spelling && result.pronunciation.empty(), "raw Enter submission became learnable");

        Lexicon repetitive; repetitive.add({"阿", "a", 10000});
        check(repetitive.decode(std::string(32, 'a')).front().text.size() == 96, "32 syllable boundary failed");
        check(repetitive.decode(std::string(33, 'a')).empty() && repetitive.decode(std::string(128, 'a')).empty(), "sentence length bound failed");
        const auto started = std::chrono::steady_clock::now();
        std::string ambiguous; for (int i = 0; i < 32; ++i) ambiguous += "shi";
        for (int i = 0; i < 8; ++i) check(lexicon.decode(ambiguous).size() <= 5, "stress result limit failed");
        lexicon.decode(std::string(128, 'a'));
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
        check(elapsed < 5000, "ambiguous sentence decoding exceeded its bounded work budget");

        testing::TestWorkspace workspace;
        const auto user_path = default_user_path();
        LearningDictionary learning(argv[1], user_path); learning.refresh();
        check(learning.enabled() && learning.users().empty(), "learning test isolation failed");
        type(input, learning.lexicon(), spelling, learning.users());
        result = input.handle(InputKey::space, 0, learning.lexicon(), learning.users());
        check(learning.remember(result) && learning.users().at({reading, chinese}) == 1, "selected sentence was not learned");
        LearningDictionary restarted(argv[1], user_path); restarted.refresh();
        const auto learned = restarted.lexicon().decode(spelling, restarted.users());
        check(learned.front().text == chinese && !learned.front().synthesized, "restart did not reuse the learned sentence as a word");
        std::string long_spelling; for (int i = 0; i < 17; ++i) long_spelling += "wo";
        input.clear(); type(input, restarted.lexicon(), long_spelling, restarted.users());
        result = input.handle(InputKey::space, 0, restarted.lexicon(), restarted.users());
        const auto saved = restarted.users();
        check(result.action == InputAction::commit && !restarted.remember(result) && restarted.users() == saved,
            "sentence decoder bypassed the 16 character learning boundary");
        std::cout << "PASS: " << checks << " sentence/correction/learning checks; stress " << elapsed << " ms\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
