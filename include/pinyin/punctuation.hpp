#pragma once
#include <string>

namespace pinyin {
struct PunctuationContext {
    std::string before; // Brief, transient context immediately before the composition/caret.
    std::string preedit;
    bool literal_scope = false;
};
struct PunctuationResult {
    std::string text;
    bool raw = false; // Submit a URL/number's raw preedit instead of selecting Chinese.
};
class Punctuation {
    bool double_open_ = true, single_open_ = true;
public:
    static bool supports(char ascii) noexcept;
    // Resolving a key does not advance quote state: only successful edits do.
    PunctuationResult resolve(char ascii, const PunctuationContext& context = {}) const;
    void accepted(const PunctuationResult& result) noexcept;
    void reset() noexcept { double_open_ = single_open_ = true; }
};
}
