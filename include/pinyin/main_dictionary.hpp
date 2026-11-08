#pragma once
#include "pinyin/lexicon.hpp"

namespace pinyin {
std::filesystem::path main_dictionary_path(const std::filesystem::path& user_path);
struct MainDictionaryMerge {
    std::size_t imported = 0, added = 0, unchanged = 0, total = 0, cleared = 0;
    std::filesystem::path path, backup, personal_backup;
};
MainDictionaryMerge merge_user_into_main(const std::filesystem::path& base_path,
    const std::filesystem::path& user_path, bool clear_after_merge = false);

struct DictionaryTotals { std::size_t texts = 0, readings = 0; };
DictionaryTotals dictionary_totals(const std::filesystem::path& base_path,
    const std::filesystem::path& user_path);

class MainDictionary {
public:
    explicit MainDictionary(std::filesystem::path path) : path_(std::move(path)) {}
    bool refresh();
    Lexicon merged(const Lexicon& base) const;
private:
    std::filesystem::path path_;
    Lexicon lexicon_;
    std::uint64_t modified_ = 0, bytes_ = 0;
    bool loaded_ = false;
};
}
