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
        for (const auto& word : {"he", "she", "you", "can", "men", "hang", "an", "long", "be", "nh", "nihao", "niha", "beijin",
                                "", "helloworld", "hello world", "hello2", "'hello", "hello'"})
            check(!starts_english(word, lexicon), "ambiguous pinyin or unlisted text was classified as English");
        Lexicon customized;
        check(starts_english("date", customized), "customized dictionary lost default English detection");
        customized.add({"大特", "da te", 1});
        check(!starts_english("date", customized, {{{"da te", "大特"}, 10}}), "exact personal word was displaced by English");
        check(starts_english("Date", customized), "explicit capitalized English was blocked by a Chinese reading");
        std::cout << "PASS: " << checks << " conservative English recognition checks\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
