#include "pinyin/input_session.hpp"
#include <iostream>
#include <stdexcept>

namespace {
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
void type(pinyin::InputSession& input, const pinyin::Lexicon& lexicon, const std::string& text) {
    for (const auto ch : text) input.handle(ch == '\'' ? pinyin::InputKey::separator : pinyin::InputKey::letter, ch, lexicon);
}
std::size_t index_of(const pinyin::InputSession& input, const std::string& text) {
    for (std::size_t i = 0; i < input.candidates().size(); ++i)
        if (input.candidates()[i].text == text) return i;
    throw std::runtime_error("missing candidate: " + text);
}
}
int wmain(int argc, wchar_t* argv[]) {
    using namespace pinyin;
    try {
        if (argc != 2) throw std::runtime_error("expected dictionary");
        Lexicon lexicon; lexicon.load(argv[1]); InputSession input;
        check(input.handle(InputKey::space, 0, lexicon).action == InputAction::pass, "empty space must pass through");
        type(input, lexicon, "nihao");
        check(input.raw() == "nihao" && input.cursor() == 5, "preedit state failed");
        check(input.candidates().front().text == "你好", "candidate lookup failed");
        auto result = input.handle(InputKey::space, 0, lexicon);
        check(result.action == InputAction::commit && result.text == "你好" && input.empty(), "space commit failed");
        type(input, lexicon, "nihao"); result = input.handle(InputKey::digit, '2', lexicon);
        check(result.text == "拟好" && input.empty(), "numeric selection failed");
        type(input, lexicon, "nihao"); input.handle(InputKey::next, 0, lexicon);
        check(input.selected() == 1, "down selection failed");
        result = input.handle(InputKey::space, 0, lexicon); check(result.text == "拟好", "highlighted selection failed");
        type(input, lexicon, "nihao"); result = input.handle(InputKey::enter, 0, lexicon);
        check(result.text == "nihao" && input.empty(), "Enter should commit raw pinyin");
        type(input, lexicon, "nihao"); result = input.handle(InputKey::escape, 0, lexicon);
        check(result.action == InputAction::cancel && input.empty(), "escape cancellation failed");
        type(input, lexicon, "nihao"); input.handle(InputKey::backspace, 0, lexicon);
        check(input.raw() == "niha" && input.candidates()[index_of(input, "你")].input_end == 2,
            "backspace prefix lookup was stale");
        input.clear(); type(input, lexicon, "niho"); input.handle(InputKey::left, 0, lexicon);
        input.handle(InputKey::letter, 'a', lexicon);
        check(input.raw() == "nihao" && input.cursor() == 4, "middle insertion failed");
        input.handle(InputKey::delete_forward, 0, lexicon);
        check(input.raw() == "niha", "forward delete failed");
        input.handle(InputKey::home, 0, lexicon); input.handle(InputKey::backspace, 0, lexicon);
        check(input.raw() == "niha" && input.cursor() == 0, "backspace before start must be harmless");
        input.handle(InputKey::end, 0, lexicon); input.handle(InputKey::letter, 'o', lexicon);
        check(input.candidates().front().text == "你好", "end navigation failed");
        input.clear(); type(input, lexicon, "xi'");
        check(input.raw() == "xi'" && input.candidates().empty(), "unfinished separator should be editable");
        type(input, lexicon, "an"); check(input.candidates().front().text == "西安", "explicit segmentation failed");
        input.clear(); type(input, lexicon, "n"); result = input.handle(InputKey::backspace, 0, lexicon);
        check(result.action == InputAction::cancel && input.empty(), "deleting final letter must end composition");
        type(input, lexicon, "nihao");
        check(input.handle(InputKey::digit, '9', lexicon).action == InputAction::pass, "invalid number must pass through");
        Lexicon empty;
        input.clear(); type(input, empty, std::string(1024, 'a'));
        check(input.handle(InputKey::letter, 'a', empty).action == InputAction::update && input.raw().size() == 1025,
            "long preedit stopped accepting letters");
        input.handle(InputKey::separator, 0, empty); type(input, empty, "a");
        check(input.raw().size() == 1027 && input.raw().substr(1024) == "a'a", "long preedit rejected a syllable separator");
        input.handle(InputKey::home, 0, empty); input.handle(InputKey::delete_forward, 0, empty);
        result = input.handle(InputKey::enter, 0, empty);
        check(result.text.size() == 1026 && result.pronunciation.empty() && input.empty(), "long raw preedit could not be edited and committed");
        Lexicon pages;
        for (int i = 0; i < 22; ++i) pages.add({"词" + std::to_string(i), "ni hao", static_cast<std::uint64_t>(100 - i)});
        input.clear(); type(input, pages, "nihao"); input.handle(InputKey::page_next, 0, pages);
        check(input.page() == 1 && input.selected() == 9, "page down failed");
        result = input.handle(InputKey::digit, '2', pages); check(result.text == "词10", "page-relative numeric selection failed");
        type(input, pages, "nihao"); input.handle(InputKey::page_next, 0, pages); input.handle(InputKey::page_next, 0, pages);
        check(input.page() == 2, "last partial page failed");
        input.handle(InputKey::page_previous, 0, pages); check(input.page() == 1, "page up failed");
        result = input.select(9, pages); check(result.text == "词9", "mouse candidate selection failed");
        Lexicon many;
        for (int i = 0; i < 120; ++i) many.add({"词" + std::to_string(i), "ni hao", static_cast<std::uint64_t>(200 - i)});
        type(input, many, "nihao");
        check(input.candidates().size() == 120, "composition silently truncated candidates to 90");
        for (int i = 0; i < 13; ++i) input.handle(InputKey::page_next, 0, many);
        result = input.handle(InputKey::digit, '3', many);
        check(result.text == "词119" && input.empty(), "late-page candidate could not be selected");

        Lexicon segmented;
        segmented.add({"如", "ru", 20}); segmented.add({"入", "ru", 10});
        segmented.add({"何", "he", 20}); segmented.add({"和", "he", 10});
        segmented.add({"你", "ni", 20}); segmented.add({"好", "hao", 20});
        segmented.add({"重庆", "chong qing", 20}); segmented.add({"何", "he", 20});
        input.clear(); type(input, segmented, "ruhe");
        check(input.candidates()[index_of(input, "如")].input_end == 2,
            "unknown phrase did not expose its first syllable");
        result = input.select(index_of(input, "如"), segmented);
        check(result.action == InputAction::update && result.pronunciation.empty() && input.preedit() == "如he" &&
            input.raw() == "he" && input.cursor() == 2, "prefix selection discarded the remainder");
        check(input.candidates().front().text == "何", "second syllable did not get independent candidates");
        result = input.handle(InputKey::space, 0, segmented);
        check(result.action == InputAction::commit && result.text == "如何" && result.pronunciation == "ru he" && input.empty(),
            "segmented phrase did not commit with the selected pronunciation");

        type(input, segmented, "ru'he"); input.select(index_of(input, "如"), segmented);
        result = input.select(index_of(input, "何"), segmented);
        check(result.text == "如何" && result.pronunciation == "ru he", "apostrophe consumption lost an input segment");
        type(input, segmented, "ruhe"); input.select(index_of(input, "如"), segmented);
        input.handle(InputKey::backspace, 0, segmented); input.handle(InputKey::backspace, 0, segmented);
        check(input.preedit() == "如" && !input.empty(), "deleting the remainder cancelled confirmed segments");
        input.handle(InputKey::backspace, 0, segmented);
        check(input.raw() == "ru" && input.confirmed_text().empty(), "backspace at the segment boundary did not undo selection");
        input.clear(); type(input, segmented, "ruhe"); input.select(index_of(input, "如"), segmented);
        input.handle(InputKey::left, 0, segmented); input.handle(InputKey::left, 0, segmented); input.handle(InputKey::left, 0, segmented);
        check(input.raw() == "ruhe" && input.cursor() == 1 && input.confirmed_text().empty(), "left navigation could not re-edit a selected segment");
        input.clear(); type(input, segmented, "ruhe"); input.select(index_of(input, "如"), segmented);
        input.handle(InputKey::home, 0, segmented);
        check(input.raw() == "ruhe" && input.cursor() == 0, "Home did not restore all original spelling");
        input.clear(); type(input, segmented, "ruhe"); input.select(index_of(input, "如"), segmented);
        result = input.handle(InputKey::enter, 0, segmented);
        check(result.text == "如he" && result.pronunciation.empty(), "raw Enter submission became a learnable phrase");
        type(input, segmented, "ruhe"); input.select(index_of(input, "如"), segmented);
        result = input.handle(InputKey::escape, 0, segmented);
        check(result.action == InputAction::cancel && input.empty() && result.pronunciation.empty(), "mixed preedit cancellation retained a word");
        type(input, segmented, "ruvi"); check(input.candidates().empty(), "invalid suffix offered a convertible prefix");
        input.clear(); type(input, segmented, "rux"); input.select(index_of(input, "如"), segmented);
        check(input.preedit() == "如x" && input.candidates().empty(), "unfinished second syllable was not preserved");
        input.handle(InputKey::letter, 'i', segmented); // No xi entry in this miniature lexicon.
        check(input.raw() == "xi", "typing after prefix confirmation failed");
        input.clear(); type(input, segmented, "chongqinghe");
        input.select(index_of(input, "重庆"), segmented); result = input.select(index_of(input, "何"), segmented);
        check(result.text == "重庆何" && result.pronunciation == "chong qing he", "multi-syllable prefix lost its phrase-level reading");
        segmented.add({"如何", "ru he", 1});
        type(input, segmented, "ruhe");
        check(input.candidates().front().text == "如何", "whole learned word must precede higher-weight single-character prefixes");
        result = input.handle(InputKey::space, 0, segmented);
        check(result.text == "如何" && result.pronunciation == "ru he", "whole-word selection regression");
        std::cout << "PASS: " << checks << " composition state checks\n"; return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
