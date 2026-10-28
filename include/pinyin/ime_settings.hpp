#pragma once
#include "pinyin/learning_dictionary.hpp"
#include <cstdint>

namespace pinyin {
struct ImeSettings {
    bool learning = true;
    bool chinese_punctuation = true;
    bool automatic_english = true;
    bool rotate_icons = false;
};
// Independent opt-out markers preserve concurrent changes to other settings
// and remain compatible with the existing command-line learning switch.
ImeSettings read_ime_settings(const std::filesystem::path& user_path);
std::filesystem::path punctuation_flag(const std::filesystem::path& user_path);
void set_chinese_punctuation(const std::filesystem::path& user_path, bool enabled);
std::filesystem::path automatic_english_flag(const std::filesystem::path& user_path);
void set_automatic_english(const std::filesystem::path& user_path, bool enabled);
std::filesystem::path icon_rotation_flag(const std::filesystem::path& user_path);
void set_icon_rotation(const std::filesystem::path& user_path, bool enabled);
// Colors use the Windows COLORREF layout (0x00BBGGRR).
struct CandidateColors {
    bool custom = false;
    std::uint32_t background = 0xffffff;
    std::uint32_t selection = 0xd77800;
};
std::filesystem::path candidate_colors_path(const std::filesystem::path& user_path);
CandidateColors read_candidate_colors(const std::filesystem::path& user_path) noexcept;
void set_candidate_colors(const std::filesystem::path& user_path, CandidateColors colors);
}
