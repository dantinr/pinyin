#include "pinyin/english_input.hpp"
#include <iostream>
#include <stdexcept>

int wmain(int argc, wchar_t* argv[]) {
    using namespace pinyin;
    int checks = 0;
    auto check = [&](bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); };
    try {
        check(argc == 2, "expected dictionary"); Lexicon lexicon; lexicon.load(argv[1]);
        for (const auto& word : {"hello", "world", "thanks", "computer", "how", "don't", "hi", "ok", "Hello", "HELLO", "I", "You"})
            check(starts_english(word, lexicon), "clear English word was not recognized");
        for (const auto& word : {"he", "she", "you", "can", "men", "hang", "an", "long", "be", "nh", "nihao", "niha", "beijin", "went", "wenti", "want",
                                "", "helloworld", "hello world", "hello2", "'hello", "hello'"})
            check(!starts_english(word, lexicon), "ambiguous pinyin or unlisted text was classified as English");
        Lexicon customized;
        check(starts_english("date", customized), "customized dictionary lost default English detection");
        customized.add({"大特", "da te", 1});
        check(!starts_english("date", customized, {{{"da te", "大特"}, 10}}), "exact personal word was displaced by English");
        check(starts_english("Date", customized), "explicit capitalized English was blocked by a Chinese reading");
        Lexicon mixed;
        check(starts_english("went", mixed) && starts_english("want", mixed), "unambiguous English was blocked without Chinese entries");
        mixed.add({"问题", "wen ti", 1}); mixed.add({"晚托", "wan tuo", 1});
        check(!starts_english("went", mixed) && !starts_english("want", mixed),
            "a lowercase English word displaced a stored mixed-pinyin candidate");
        check(!starts_english("went", mixed, {{{"wen ti", "问题"}, 20}}),
            "personal usage failed to protect a mixed-pinyin candidate");
        check(starts_english("Went", mixed) && starts_english("Want", mixed),
            "explicit uppercase English lost precedence over mixed-pinyin candidates");
        std::cout << "PASS: " << checks << " conservative English recognition checks\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
