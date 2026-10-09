#pragma once
#include "pinyin/lexicon.hpp"

namespace pinyin {
enum class InputKey { letter, separator, backspace, delete_forward, left, right, home, end,
                      previous, next, page_previous, page_next, space, enter, escape, digit };
enum class InputAction { pass, update, commit, cancel };
struct InputResult {
    InputAction action = InputAction::pass;
    std::string text;
    // Nonempty only for a completely confirmed candidate selection.
    std::string pronunciation;
};

// Platform-independent composition state. No persistence or application text access.
class InputSession {
public:
    static constexpr std::size_t page_size = 9;
    InputResult handle(InputKey key, char value, const Lexicon& lexicon, const UserDictionary& users = {});
    InputResult select(std::size_t index, const Lexicon& lexicon, const UserDictionary& users = {});
    void clear() noexcept;
    const std::string& raw() const noexcept { return raw_; }
    std::string confirmed_text() const;
    std::string preedit() const { return confirmed_text() + raw_; }
    std::size_t spelling_size() const noexcept;
    const std::vector<Candidate>& candidates() const noexcept { return candidates_; }
    std::size_t cursor() const noexcept { return cursor_; }
    std::size_t selected() const noexcept { return selected_; }
    std::size_t page() const noexcept { return selected_ / page_size; }
    bool empty() const noexcept { return raw_.empty() && segments_.empty(); }
private:
    struct Segment { Candidate word; std::string spelling; };
    std::vector<Segment> segments_;
    std::string raw_;
    std::vector<Candidate> candidates_;
    std::size_t cursor_ = 0;
    std::size_t selected_ = 0;
    void refresh(const Lexicon& lexicon, const UserDictionary& users);
    void undo_segment();
    std::string confirmed_pronunciation() const;
};
}
