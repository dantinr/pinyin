#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace pinyin {

struct Entry {
    std::string text;
    std::string pronunciation; // Canonical, space-separated syllables.
    std::uint64_t weight = 0;
};

struct Candidate : Entry {
    std::uint64_t selections = 0;
    double score = 0;
};

// A user record is a word and its pronunciation, never a keystroke history.
using UserKey = std::pair<std::string, std::string>; // pronunciation, text
using UserDictionary = std::map<UserKey, std::uint64_t>;

std::string normalize_query(const std::string& input);
std::string normalize_pronunciation(const std::string& input);
void validate_text(const std::string& text);

class Lexicon {
public:
    // Strict TSV: text<TAB>pronunciation<TAB>positive weight. BOM/comments supported.
    // Loading is transactional: invalid input leaves the current lexicon intact.
    void load(const std::filesystem::path& path);
    void add(Entry entry);
    std::vector<Candidate> lookup(const std::string& input,
                                  const UserDictionary& users = {},
                                  std::size_t limit = 10) const;
    std::size_t size() const noexcept { return entries_.size(); }

private:
    struct Node {
        std::map<std::string, std::size_t> next;
        std::vector<std::size_t> entries;
    };
    std::vector<Node> nodes_{1};
    std::vector<Entry> entries_;
    std::map<UserKey, std::size_t> entry_index_;
    void match(std::size_t node, std::size_t offset, const std::string& query,
               std::vector<std::size_t>& result) const;
};

void learn(UserDictionary& users, const Candidate& candidate);

} // namespace pinyin
