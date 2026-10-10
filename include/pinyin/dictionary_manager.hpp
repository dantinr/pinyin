#pragma once
#include "pinyin/lexicon.hpp"

namespace pinyin {
std::filesystem::path default_dictionary_directory();
bool valid_dictionary_name(const std::string& name) noexcept;
struct DictionaryFile {
    Lexicon lexicon;
    std::vector<std::string> comments;
    std::size_t records = 0;
};
DictionaryFile read_dictionary_file(const std::filesystem::path& path);
struct DictionaryInfo {
    std::string name;
    std::filesystem::path path;
    bool enabled = true;
    std::size_t entries = 0;
    std::string error;
};

// Explicit local edits. Each transaction locks the supplementary directory and
// replaces only the named dictionary; it never opens the learned user store.
class DictionaryManager {
public:
    explicit DictionaryManager(std::filesystem::path root);
    const std::filesystem::path& root() const noexcept { return root_; }
    std::vector<DictionaryInfo> list() const;
    std::size_t import_file(const std::string& name, const std::filesystem::path& source, bool replace = false);
    std::size_t upsert(const std::string& name, Entry word);
    bool remove(const std::string& name, const std::string& text, const std::string& pronunciation);
    void set_enabled(const std::string& name, bool enabled);
    void export_file(const std::string& name, const std::filesystem::path& target) const;
private:
    std::filesystem::path root_;
    std::filesystem::path path(const std::string& name) const;
};

// Read-only cache for the IME. No directory or lock file is created by lookup.
// Bad files are skipped independently; repairs and enable/disable changes reload.
class SupplementaryDictionaries {
public:
    explicit SupplementaryDictionaries(std::filesystem::path root) : root_(std::move(root)) {}
    bool refresh();
    Lexicon merged(const Lexicon& base) const;
private:
    struct Cached {
        std::uint64_t modified = 0, bytes = 0;
        bool enabled = true;
        Lexicon lexicon;
    };
    std::filesystem::path root_;
    std::map<std::string, Cached> files_;
};
}
