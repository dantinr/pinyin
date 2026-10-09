#include "pinyin/punctuation.hpp"
#include "pinyin/learning_dictionary.hpp"
#include "test_workspace.hpp"
#include <iostream>

namespace {
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
void type(pinyin::InputSession& input, const pinyin::Lexicon& lexicon, const std::string& text) {
    for (const auto ch : text) input.handle(ch == '\'' ? pinyin::InputKey::separator : pinyin::InputKey::letter, ch, lexicon);
}
std::size_t index_of(const pinyin::InputSession& input, const std::string& text) {
    for (std::size_t i = 0; i < input.candidates().size(); ++i) if (input.candidates()[i].text == text) return i;
    throw std::runtime_error("missing candidate: " + text);
}
}
int wmain(int argc, wchar_t* argv[]) {
    using namespace pinyin;
    try {
        if (argc != 2) throw std::runtime_error("expected dictionary");
        Punctuation punctuation;
        for (const auto& pair : {std::pair{',', "，"}, {'.', "。"}, {'?', "？"}, {'!', "！"},
                                {':', "："}, {';', "；"}, {'\\', "、"}, {'(', "（"}, {')', "）"},
                                {'[', "【"}, {']', "】"}, {'<', "《"}, {'>', "》"}, {'_', "——"}, {'^', "……"}})
            check(Punctuation::supports(pair.first) && punctuation.resolve(pair.first).text == pair.second,
                "Chinese punctuation mapping failed");
        check(!Punctuation::supports('a') && !Punctuation::supports('0'), "ordinary input classified as punctuation");
        check(punctuation.resolve('"').text == "“" && punctuation.resolve('"').text == "“", "query advanced quote state");
        punctuation.accepted(punctuation.resolve('"'));
        check(punctuation.resolve('"').text == "”", "double quote did not close");
        punctuation.accepted(punctuation.resolve('\''));
        check(punctuation.resolve('\'').text == "’" && punctuation.resolve('"').text == "”", "single/double quote states interfered");
        punctuation.accepted(punctuation.resolve('"', {"https://example.com/", {}, false}));
        check(punctuation.resolve('"').text == "”", "ASCII quote advanced Chinese quote state");
        punctuation.reset(); check(punctuation.resolve('"').text == "“" && punctuation.resolve('\'').text == "‘", "quote reset failed");
        check(punctuation.resolve('.', {"3", {}, false}).text == ".", "decimal point converted to Chinese");
        check(punctuation.resolve(':', {"12", {}, false}).text == ":", "time colon converted to Chinese");
        check(punctuation.resolve('.', {"3.14", "nh", false}).text == "。", "number prevented a subsequent Chinese word");
        check(punctuation.resolve('.', {"English", {}, false}).text == ".", "English word lost its period");
        check(punctuation.resolve('.', {{}, "nihao", false}).text == "。", "preedit was mistaken for an English token");
        check(punctuation.resolve(':', {{}, "https", false}).raw &&
            punctuation.resolve('.', {{}, "www", false}).raw, "URL prefix would select a Chinese candidate");
        for (const auto& token : {"https://example.com", "a@example.com", "C:\\path\\name", "www.example.com"})
            check(punctuation.resolve('?', {token, "query", false}).text == "?" &&
                punctuation.resolve('?', {token, "query", false}).raw, "literal token punctuation changed");
        check(punctuation.resolve('?', {"https://example.com 中文", "nh", false}).text == "？", "literal token leaked past a boundary");

        Lexicon lexicon; lexicon.load(argv[1]); InputSession input;
        type(input, lexicon, "nh"); auto result = input.confirm_remaining(lexicon);
        check(result.text == "你好" && result.pronunciation == "ni hao" && input.empty(), "punctuation could not confirm a complete candidate");
        type(input, lexicon, "nihao"); input.handle(InputKey::next, 0, lexicon);
        result = input.confirm_remaining(lexicon);
        check(result.text == "拟好" && result.pronunciation == "ni hao", "punctuation ignored highlighted candidate");
        Lexicon segments;
        segments.add({"你", "ni", 100}); segments.add({"好", "hao", 100}); segments.add({"北京", "bei jing", 100});
        type(input, segments, "nhbj"); input.select(index_of(input, "你"), segments);
        result = input.confirm_remaining(segments);
        check(result.text == "你好北京" && result.pronunciation == "ni hao bei jing", "punctuation dropped unconfirmed segments");
        Lexicon small; small.add({"如", "ru", 10}); small.add({"何", "he", 10});
        type(input, small, "rux"); input.select(index_of(input, "如"), small); result = input.confirm_remaining(small);
        check(result.text == "如x" && result.pronunciation.empty() && input.empty(), "invalid remainder was dropped or learned");
        type(input, small, "ru'he"); result = input.confirm_remaining(small);
        check(result.text == "如何" && result.pronunciation == "ru he", "separator prevented punctuation confirmation");
        type(input, lexicon, "xi'"); result = input.confirm_remaining(lexicon);
        check(result.text == "xi'" && result.pronunciation.empty(), "unfinished separator was discarded");

        testing::TestWorkspace workspace;
        LearningDictionary learning(argv[1], default_user_path()); learning.refresh();
        type(input, learning.lexicon(), "nh"); result = input.confirm_remaining(learning.lexicon(), learning.users());
        check(learning.remember(result) && learning.users().at({"ni hao", "你好"}) == 1 && learning.users().size() == 1,
            "punctuation confirmation did not learn the word without its punctuation");
        LearningDictionary restarted(argv[1], default_user_path()); restarted.refresh();
        check(restarted.users() == learning.users(), "punctuation-confirmed word did not survive restart");
        std::cout << "PASS: " << checks << " punctuation/context/confirmation checks\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1; }
}
