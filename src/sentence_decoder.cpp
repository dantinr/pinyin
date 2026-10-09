#include "pinyin/lexicon.hpp"
#include <algorithm>
#include <cmath>

namespace pinyin {
namespace {
constexpr std::size_t beam_width = 16;
constexpr std::size_t edges_per_end = 16;

struct Path {
    std::string text, pronunciation, last_single, last_reading;
    double score = 0;
    std::size_t words = 0;
};
bool better(const Path& a, const Path& b) {
    if (a.score != b.score) return a.score > b.score;
    if (a.words != b.words) return a.words < b.words;
    if (a.text != b.text) return a.text < b.text;
    return a.pronunciation < b.pronunciation;
}
void keep(std::vector<Path>& beam, Path path) {
    const auto existing = std::find_if(beam.begin(), beam.end(), [&](const auto& old) { return old.text == path.text; });
    if (existing != beam.end()) {
        if (!better(path, *existing)) return;
        beam.erase(existing);
    }
    const auto position = std::lower_bound(beam.begin(), beam.end(), path, better);
    if (position == beam.end() && beam.size() == beam_width) return;
    beam.insert(position, std::move(path));
    if (beam.size() > beam_width) beam.pop_back();
}
struct Edge {
    std::size_t id, end;
    double score;
    bool single;
};
}

std::vector<Candidate> Lexicon::decode(const std::string& input,
                                      const UserDictionary& users, std::size_t limit) const {
    return decode_normalized(normalize_query(input), users, limit);
}
std::vector<Candidate> Lexicon::decode_normalized(const std::string& query,
                                                 const UserDictionary& users, std::size_t limit) const {
    auto result = lookup(query, users, limit);
    for (auto& word : result) word.input_end = query.size();
    if (result.size() == limit) return result;

    std::vector<std::vector<Path>> paths(query.size() + 1);
    paths[0].push_back({});
    for (std::size_t start = 0; start < query.size(); ++start) {
        if (paths[start].empty() || query[start] == '\'') continue;
        std::vector<std::pair<std::size_t, std::size_t>> matches;
        match_prefixes(0, start, query, matches);
        std::map<std::size_t, std::vector<Edge>> ends;
        for (const auto& match : matches) {
            const auto& word = entries_[match.first];
            const auto count = static_cast<std::size_t>(std::count(word.pronunciation.begin(), word.pronunciation.end(), ' ') + 1);
            const auto user = users.find({word.pronunciation, word.text});
            const auto uses = user == users.end() ? 0 : user->second;
            // A personal choice has an immediate effect inside sentences too,
            // including a new word whose base weight is only one.
            const auto weight = uses ? std::max<std::uint64_t>(word.weight, 12000) : word.weight;
            // Center the heuristic weights and penalize word boundaries. This
            // favors known multi-character words over arbitrary single letters
            // without rewarding extra syllables in an ambiguous segmentation.
            const auto score = count * (std::log1p(static_cast<double>(weight)) - std::log1p(12000.0)) +
                2.0 * std::log1p(static_cast<double>(uses)) - 2.0;
            const bool single = count == 1 && std::count_if(word.text.begin(), word.text.end(), [](unsigned char c) {
                return (c & 0xc0) != 0x80;
            }) == 1;
            ends[match.second].push_back({match.first, match.second, score, single});
        }
        for (auto& end : ends) {
            auto& edges = end.second;
            std::sort(edges.begin(), edges.end(), [&](const auto& a, const auto& b) {
                if (a.score != b.score) return a.score > b.score;
                return entries_[a.id].text < entries_[b.id].text;
            });
            if (edges.size() > edges_per_end) edges.resize(edges_per_end);
            for (const auto& edge : edges) {
                const auto& word = entries_[edge.id];
                for (const auto& prefix : paths[start]) {
                    auto& beam = paths[edge.end];
                    const auto score = prefix.score + edge.score;
                    if (beam.size() == beam_width && score < beam.back().score) continue;
                    // Do not invent a different reading for a known two-character
                    // word by joining its polyphonic characters, e.g. 重/庆.
                    if (edge.single && !prefix.last_single.empty()) {
                        const auto known = word_readings_.find(prefix.last_single + word.text);
                        if (known != word_readings_.end() &&
                            !known->second.count(prefix.last_reading + ' ' + word.pronunciation)) continue;
                    }
                    Path next;
                    next.text = prefix.text + word.text;
                    next.pronunciation = prefix.pronunciation.empty() ? word.pronunciation : prefix.pronunciation + ' ' + word.pronunciation;
                    next.score = score; next.words = prefix.words + 1;
                    if (edge.single) { next.last_single = word.text; next.last_reading = word.pronunciation; }
                    keep(beam, std::move(next));
                }
            }
        }
        // Edges always point forward. Release completed beams so long input
        // does not retain a copy of every sentence prefix.
        paths[start].clear();
    }
    for (auto& path : paths.back()) {
        if (path.words < 2) continue;
        // Whole-word readings stay authoritative even when there are multiple
        // possible ways to assemble the same text from shorter dictionary words.
        const auto known = word_readings_.find(path.text);
        if (known != word_readings_.end()) continue;
        if (std::any_of(result.begin(), result.end(), [&](const auto& word) { return word.text == path.text; })) continue;
        Candidate word;
        word.text = std::move(path.text); word.pronunciation = std::move(path.pronunciation);
        word.score = path.score; word.input_end = query.size(); word.synthesized = true;
        result.push_back(std::move(word));
        if (result.size() == limit) break;
    }
    return result;
}
}
