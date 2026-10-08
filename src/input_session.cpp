#include "pinyin/input_session.hpp"
#include <algorithm>
#include <stdexcept>

namespace pinyin {
void InputSession::clear() noexcept {
    raw_.clear(); candidates_.clear(); cursor_ = 0; selected_ = 0;
}
void InputSession::refresh(const Lexicon& lexicon) {
    candidates_.clear(); selected_ = 0;
    if (raw_.empty()) return;
    try { candidates_ = lexicon.lookup(raw_, {}, 90); }
    catch (const std::invalid_argument&) {} // An unfinished apostrophe is a valid editing state.
}
InputResult InputSession::select(std::size_t index) {
    if (index >= candidates_.size()) return {};
    auto text = candidates_[index].text;
    clear();
    return {InputAction::commit, std::move(text)};
}
InputResult InputSession::handle(InputKey key, char value, const Lexicon& lexicon) {
    if (key == InputKey::letter) {
        if (value < 'a' || value > 'z' || raw_.size() >= 128) return {};
        raw_.insert(cursor_++, 1, value); refresh(lexicon);
        return {InputAction::update, {}};
    }
    if (empty()) return {};
    switch (key) {
    case InputKey::separator:
        if (raw_.size() < 128 && cursor_ > 0 && raw_[cursor_ - 1] != '\'' &&
            (cursor_ == raw_.size() || raw_[cursor_] != '\'')) {
            raw_.insert(cursor_++, 1, '\''); refresh(lexicon);
        }
        break;
    case InputKey::backspace:
        if (cursor_) { raw_.erase(--cursor_, 1); refresh(lexicon); }
        break;
    case InputKey::delete_forward:
        if (cursor_ < raw_.size()) { raw_.erase(cursor_, 1); refresh(lexicon); }
        break;
    case InputKey::left: if (cursor_) --cursor_; break;
    case InputKey::right: if (cursor_ < raw_.size()) ++cursor_; break;
    case InputKey::home: cursor_ = 0; break;
    case InputKey::end: cursor_ = raw_.size(); break;
    case InputKey::previous: if (selected_) --selected_; break;
    case InputKey::next: if (selected_ + 1 < candidates_.size()) ++selected_; break;
    case InputKey::page_previous:
        if (page()) selected_ = (page() - 1) * page_size;
        break;
    case InputKey::page_next:
        if ((page() + 1) * page_size < candidates_.size()) selected_ = (page() + 1) * page_size;
        break;
    case InputKey::digit:
        if (value >= '1' && value <= '9') return select(page() * page_size + value - '1');
        return {};
    case InputKey::space:
        if (!candidates_.empty()) return select(selected_);
        [[fallthrough]];
    case InputKey::enter: {
        auto text = raw_; clear(); return {InputAction::commit, std::move(text)};
    }
    case InputKey::escape: clear(); return {InputAction::cancel, {}};
    default: return {};
    }
    return {empty() ? InputAction::cancel : InputAction::update, {}};
}
}
