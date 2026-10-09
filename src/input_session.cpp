#include "pinyin/input_session.hpp"
#include <algorithm>
#include <stdexcept>

namespace pinyin {
void InputSession::clear() noexcept {
    raw_.clear(); segments_.clear(); candidates_.clear(); cursor_ = 0; selected_ = 0;
}
std::string InputSession::confirmed_text() const {
    std::string text;
    for (const auto& segment : segments_) text += segment.word.text;
    return text;
}
std::string InputSession::confirmed_pronunciation() const {
    std::string pronunciation;
    for (const auto& segment : segments_) {
        if (!pronunciation.empty()) pronunciation += ' ';
        pronunciation += segment.word.pronunciation;
    }
    return pronunciation;
}
std::size_t InputSession::spelling_size() const noexcept {
    auto size = raw_.size();
    for (const auto& segment : segments_) size += segment.spelling.size();
    return size;
}
void InputSession::undo_segment() {
    if (segments_.empty()) return;
    auto segment = std::move(segments_.back()); segments_.pop_back();
    raw_.insert(0, segment.spelling);
    cursor_ += segment.spelling.size();
}
void InputSession::refresh(const Lexicon& lexicon, const UserDictionary& users) {
    candidates_.clear(); selected_ = 0;
    if (raw_.empty()) return;
    try { candidates_ = lexicon.lookup_composition(raw_, users); }
    catch (const std::invalid_argument&) {} // An unfinished apostrophe is a valid editing state.
}
InputResult InputSession::select(std::size_t index, const Lexicon& lexicon, const UserDictionary& users) {
    if (index >= candidates_.size()) return {};
    const auto word = candidates_[index];
    if (!word.input_end || word.input_end > raw_.size()) return {};
    segments_.push_back({word, raw_.substr(0, word.input_end)});
    raw_.erase(0, word.input_end); cursor_ = raw_.size();
    if (!raw_.empty()) {
        refresh(lexicon, users);
        return {InputAction::update, {}, {}};
    }
    auto text = confirmed_text(), pronunciation = confirmed_pronunciation();
    clear();
    return {InputAction::commit, std::move(text), std::move(pronunciation)};
}
InputResult InputSession::handle(InputKey key, char value, const Lexicon& lexicon, const UserDictionary& users) {
    if (key == InputKey::letter) {
        if (value < 'a' || value > 'z') return {};
        raw_.insert(cursor_++, 1, value); refresh(lexicon, users);
        return {InputAction::update, {}, {}};
    }
    if (empty()) return {};
    switch (key) {
    case InputKey::separator:
        if (cursor_ > 0 && raw_[cursor_ - 1] != '\'' &&
            (cursor_ == raw_.size() || raw_[cursor_] != '\'')) {
            raw_.insert(cursor_++, 1, '\''); refresh(lexicon, users);
        }
        break;
    case InputKey::backspace:
        if (cursor_) { raw_.erase(--cursor_, 1); refresh(lexicon, users); }
        else if (!segments_.empty()) { undo_segment(); refresh(lexicon, users); }
        break;
    case InputKey::delete_forward:
        if (cursor_ < raw_.size()) { raw_.erase(cursor_, 1); refresh(lexicon, users); }
        break;
    case InputKey::left:
        if (!cursor_ && !segments_.empty()) { undo_segment(); refresh(lexicon, users); }
        if (cursor_) --cursor_;
        break;
    case InputKey::right: if (cursor_ < raw_.size()) ++cursor_; break;
    case InputKey::home:
        while (!segments_.empty()) undo_segment();
        cursor_ = 0; refresh(lexicon, users);
        break;
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
        if (value >= '1' && value <= '9') return select(page() * page_size + value - '1', lexicon, users);
        return {};
    case InputKey::space:
        if (!candidates_.empty()) return select(selected_, lexicon, users);
        if (raw_.empty()) {
            auto text = confirmed_text(), pronunciation = confirmed_pronunciation();
            clear(); return {InputAction::commit, std::move(text), std::move(pronunciation)};
        }
        [[fallthrough]];
    case InputKey::enter: {
        auto text = preedit(); clear(); return {InputAction::commit, std::move(text), {}};
    }
    case InputKey::escape: clear(); return {InputAction::cancel, {}, {}};
    default: return {};
    }
    return {empty() ? InputAction::cancel : InputAction::update, {}, {}};
}
InputResult InputSession::confirm_remaining(const Lexicon& lexicon, const UserDictionary& users) {
    while (!empty()) {
        const auto result = handle(InputKey::space, 0, lexicon, users);
        if (result.action != InputAction::update) return result;
    }
    return {};
}
}
