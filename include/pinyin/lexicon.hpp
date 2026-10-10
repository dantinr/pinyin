#pragma once

#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <limits>
#include <map>
#include <set>
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
    // End offset in the normalized query; set by composition lookup and decode.
    std::size_t input_end = 0;
    std::size_t abbreviations = 0; // Syllables matched by an initial instead of full spelling.
    bool synthesized = false; // A complete candidate assembled from multiple words.
    bool completed = false; // The final typed syllable is a prefix of its full reading.
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
    void load(const std::filesystem::path& path, bool allow_empty = false);
    void load(std::istream& input, bool allow_empty = false);
    void add(Entry entry);
    std::vector<Candidate> lookup(const std::string& input,
                                  const UserDictionary& users = {},
                                  std::size_t limit = 10) const;
    // The last syllable can be completed; no following syllable is predicted.
    // Offline sentence decoding uses a bounded beam. Fewer abbreviations win;
    // equally abbreviated whole words precede synthesized sentences.
    std::vector<Candidate> decode(const std::string& input,
                                 const UserDictionary& users = {},
                                 std::size_t limit = 5) const;
    // Full matches first, then convertible prefixes with a spellable remainder.
    std::vector<Candidate> lookup_composition(const std::string& input,
                                             const UserDictionary& users = {},
                                             std::size_t limit = std::numeric_limits<std::size_t>::max()) const;
    std::size_t size() const noexcept { return entries_.size(); }
    const std::vector<Entry>& entries() const noexcept { return entries_; }

private:
    struct Match {
        std::size_t id, end, abbreviations;
        bool completed = false;
    };
    struct Node {
        std::map<std::string, std::size_t> next;
        std::vector<std::size_t> entries;
    };
    std::vector<Node> nodes_{1};
    std::vector<Entry> entries_;
    std::map<UserKey, std::size_t> entry_index_;
    std::map<std::string, std::set<std::string>> word_readings_;
    std::vector<Candidate> decode_normalized(const std::string& query,
                                            const UserDictionary& users, std::size_t limit) const;
    void match_prefixes(std::size_t node, std::size_t offset, const std::string& query,
                        std::vector<Match>& result) const;
};

void learn(UserDictionary& users, const Candidate& candidate);

} // namespace pinyin
