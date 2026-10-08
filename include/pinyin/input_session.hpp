#pragma once
#include "pinyin/lexicon.hpp"

namespace pinyin {
enum class InputKey { letter, separator, backspace, delete_forward, left, right, home, end,
                      previous, next, page_previous, page_next, space, enter, escape, digit };
enum class InputAction { pass, update, commit, cancel };
struct InputResult { InputAction action = InputAction::pass; std::string text; };

// Platform-independent composition state. No persistence or application text access.
class InputSession {
public:
    static constexpr std::size_t page_size = 9;
    InputResult handle(InputKey key, char value, const Lexicon& lexicon);
    InputResult select(std::size_t index);
    void clear() noexcept;
    const std::string& raw() const noexcept { return raw_; }
    const std::vector<Candidate>& candidates() const noexcept { return candidates_; }
    std::size_t cursor() const noexcept { return cursor_; }
    std::size_t selected() const noexcept { return selected_; }
    std::size_t page() const noexcept { return selected_ / page_size; }
    bool empty() const noexcept { return raw_.empty(); }
private:
    std::string raw_;
    std::vector<Candidate> candidates_;
    std::size_t cursor_ = 0;
    std::size_t selected_ = 0;
    void refresh(const Lexicon& lexicon);
};
}
