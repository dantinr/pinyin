#pragma once
#include "pinyin/lexicon.hpp"

namespace pinyin {
// A conservative, offline start-of-English-word check. A curated English word
// in lowercase never overrides a pinyin syllable or a stored full-pinyin match
// (including tail completion). A capitalized English word is explicit Latin.
// Sentence state belongs to the TSF session; no application text is read here.
bool starts_english(const std::string& word, const Lexicon& lexicon, const UserDictionary& users = {});
}
