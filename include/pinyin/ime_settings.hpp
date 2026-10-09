#pragma once
#include "pinyin/learning_dictionary.hpp"

namespace pinyin {
struct ImeSettings {
    bool learning = true;
    bool chinese_punctuation = true;
};
// Independent opt-out markers preserve concurrent changes to other settings
// and remain compatible with the existing command-line learning switch.
ImeSettings read_ime_settings(const std::filesystem::path& user_path);
std::filesystem::path punctuation_flag(const std::filesystem::path& user_path);
void set_chinese_punctuation(const std::filesystem::path& user_path, bool enabled);
}
