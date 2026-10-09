#include "pinyin/lexicon.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <deque>
#include <fstream>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>

namespace pinyin {
namespace {
constexpr std::uint64_t max_weight = 1000000000;

const std::set<std::string>& syllables() {
    static const std::set<std::string> values = [] {
        std::istringstream stream(
            "a ai an ang ao ba bai ban bang bao bei ben beng bi bian biao bie bin bing bo bu "
            "ca cai can cang cao ce cen ceng cha chai chan chang chao che chen cheng chi chong chou chu chua chuai chuan chuang chui chun chuo ci cong cou cu cuan cui cun cuo "
            "da dai dan dang dao de dei den deng di dia dian diao die ding diu dong dou du duan dui dun duo "
            "e ei en eng er fa fan fang fei fen feng fo fou fu "
            "ga gai gan gang gao ge gei gen geng gong gou gu gua guai guan guang gui gun guo "
            "ha hai han hang hao he hei hen heng hong hou hu hua huai huan huang hui hun huo "
            "ji jia jian jiang jiao jie jin jing jiong jiu ju juan jue jun "
            "ka kai kan kang kao ke kei ken keng kong kou ku kua kuai kuan kuang kui kun kuo "
            "la lai lan lang lao le lei leng li lia lian liang liao lie lin ling liu lo long lou lu luan lun luo lv lve "
            "m ma mai man mang mao me mei men meng mi mian miao mie min ming miu mo mou mu "
            "n na nai nan nang nao ne nei nen neng ng ni nian niang niao nie nin ning niu nong nou nu nuan nuo nv nve "
            "o ou pa pai pan pang pao pei pen peng pi pian piao pie pin ping po pou pu "
            "qi qia qian qiang qiao qie qin qing qiong qiu qu quan que qun "
            "ran rang rao re ren reng ri rong rou ru rua ruan rui run ruo "
            "sa sai san sang sao se sen seng sha shai shan shang shao she shei shen sheng shi shou shu shua shuai shuan shuang shui shun shuo si song sou su suan sui sun suo "
            "ta tai tan tang tao te teng ti tian tiao tie ting tong tou tu tuan tui tun tuo "
            "wa wai wan wang wei wen weng wo wu "
            "xi xia xian xiang xiao xie xin xing xiong xiu xu xuan xue xun "
            "ya yan yang yao ye yi yin ying yo yong you yu yuan yue yun "
            "za zai zan zang zao ze zei zen zeng zha zhai zhan zhang zhao zhe zhei zhen zheng zhi zhong zhou zhu zhua zhuai zhuan zhuang zhui zhun zhuo zi zong zou zu zuan zui zun zuo");
        std::set<std::string> result;
        for (std::string value; stream >> value;) result.insert(value);
        return result;
    }();
    return values;
}

std::uint64_t parse_weight(const std::string& value) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("weight must be a positive integer");
    std::size_t consumed = 0;
    const auto result = std::stoull(value, &consumed);
    if (consumed != value.size() || result == 0 || result > max_weight)
        throw std::runtime_error("weight must be in [1, 1000000000]");
    return result;
}

// Full spelling, first letter, and the optional zh/ch/sh initial. Dictionary
// readings remain canonical; only the spelling consumed from the query changes.
std::array<std::size_t, 3> spelling_lengths(const std::string& syllable,
                                          const std::string& query, std::size_t offset) {
    if (offset >= query.size() || query[offset] != syllable.front()) return {};
    const bool digraph = syllable.size() > 2 && syllable[1] == 'h' &&
        (syllable[0] == 'z' || syllable[0] == 'c' || syllable[0] == 's');
    return {
        query.compare(offset, syllable.size(), syllable) == 0 ? syllable.size() : 0,
        syllable.size() > 1 ? 1u : 0u,
        digraph && query.compare(offset, 2, syllable, 0, 2) == 0 ? 2u : 0u
    };
}

std::vector<bool> spellable_suffixes(const std::string& query) {
    std::vector<bool> reachable(query.size() + 1, false);
    reachable.back() = true;
    for (auto position = query.size(); position-- > 0;) {
        const auto& all = syllables();
        for (auto it = all.lower_bound(std::string(1, query[position]));
             it != all.end() && it->front() == query[position]; ++it) {
            const auto& syllable = *it;
            const auto remaining = query.size() - position;
            // The last syllable may still be in progress, e.g. niha -> ni + ha.
            if (remaining < syllable.size() && syllable.compare(0, remaining, query, position, remaining) == 0) {
                reachable[position] = true;
                break;
            }
            for (const auto length : spelling_lengths(syllable, query, position)) {
                if (!length) continue;
                auto next = position + length;
                if (next < query.size() && query[next] == '\'') ++next;
                if (reachable[next]) { reachable[position] = true; break; }
            }
            if (reachable[position]) break;
        }
    }
    return reachable;
}

Candidate ranked(const Entry& entry, const UserDictionary& users) {
    Candidate candidate;
    static_cast<Entry&>(candidate) = entry;
    const auto found = users.find({entry.pronunciation, entry.text});
    if (found != users.end()) candidate.selections = found->second;
    candidate.score = std::log1p(static_cast<double>(candidate.weight)) +
        2.0 * std::log1p(static_cast<double>(candidate.selections));
    return candidate;
}
bool preferred(const Candidate& a, const Candidate& b) {
    // Preserve full-pinyin choices when an input can also be read as initials.
    if (a.abbreviations != b.abbreviations) return a.abbreviations < b.abbreviations;
    // Exact stored readings precede completions even if the latter were learned.
    // Synthesized sentences use their word scores and a completion penalty.
    if (!a.synthesized && !b.synthesized && a.completed != b.completed) return !a.completed;
    // A confirmed personal word is useful immediately, even if its base weight
    // is low. Frequency breaks ties among learned words and among other words.
    if (!!a.selections != !!b.selections) return !!a.selections;
    if (a.score != b.score) return a.score > b.score;
    return a.text < b.text;
}
} // namespace

std::string normalize_query(const std::string& input) {
    std::string result;
    bool boundary = false;
    bool apostrophe = false;
    for (std::size_t i = 0; i < input.size(); ++i) {
        const auto c = static_cast<unsigned char>(input[i]);
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n') {
            if (!result.empty()) boundary = true;
            continue;
        }
        if (c == '\'') {
            if (result.empty() || apostrophe)
                throw std::invalid_argument("invalid apostrophe boundary");
            boundary = true;
            apostrophe = true;
            continue;
        }
        char letter = 0;
        if (c >= 'a' && c <= 'z') letter = static_cast<char>(c);
        else if (c >= 'A' && c <= 'Z') letter = static_cast<char>(c - 'A' + 'a');
        else if (c == 0xc3 && i + 1 < input.size() &&
                 (static_cast<unsigned char>(input[i + 1]) == 0xbc ||
                  static_cast<unsigned char>(input[i + 1]) == 0x9c)) {
            letter = 'v';
            ++i;
        } else throw std::invalid_argument("use letters, spaces, apostrophes, or ue-diaeresis (v); tones are not supported");
        if (boundary) result.push_back('\'');
        result.push_back(letter);
        boundary = false;
        apostrophe = false;
    }
    if (apostrophe) throw std::invalid_argument("pinyin cannot end with an apostrophe");
    if (result.empty()) throw std::invalid_argument("pinyin must not be empty");
    return result;
}

std::string normalize_pronunciation(const std::string& input) {
    auto result = normalize_query(input);
    std::replace(result.begin(), result.end(), '\'', ' ');
    std::istringstream stream(result);
    for (std::string syllable; stream >> syllable;) {
        if (syllables().count(syllable) == 0)
            throw std::invalid_argument("invalid syllable: " + syllable + "; separate syllables with spaces");
    }
    return result;
}

void validate_text(const std::string& text) {
    if (text.empty()) throw std::invalid_argument("word must not be empty");
    for (std::size_t i = 0; i < text.size();) {
        auto c = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        std::uint32_t code = 0;
        if (c < 0x80) { length = 1; code = c; }
        else if (c >= 0xc2 && c <= 0xdf) { length = 2; code = c & 0x1f; }
        else if (c >= 0xe0 && c <= 0xef) { length = 3; code = c & 0x0f; }
        else if (c >= 0xf0 && c <= 0xf4) { length = 4; code = c & 0x07; }
        else throw std::invalid_argument("invalid UTF-8 word");
        if (i + length > text.size()) throw std::invalid_argument("truncated UTF-8 word");
        for (std::size_t j = 1; j < length; ++j) {
            c = static_cast<unsigned char>(text[i + j]);
            if ((c & 0xc0) != 0x80) throw std::invalid_argument("invalid UTF-8 continuation");
            code = (code << 6) | (c & 0x3f);
        }
        if ((length == 2 && code < 0x80) || (length == 3 && code < 0x800) ||
            (length == 4 && code < 0x10000) || code > 0x10ffff ||
            (code >= 0xd800 && code <= 0xdfff) || code < 0x20 ||
            (code >= 0x7f && code <= 0x9f))
            throw std::invalid_argument("invalid UTF-8 or control character in word");
        i += length;
    }
}

void Lexicon::load(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) throw std::runtime_error("cannot open base dictionary");
    Lexicon loaded;
    std::size_t line_number = 0;
    for (std::string line; std::getline(file, line);) {
        ++line_number;
        if (line_number == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line.front() == '#') continue;
        try {
            const auto first = line.find('\t');
            const auto second = first == std::string::npos ? first : line.find('\t', first + 1);
            if (first == std::string::npos || second == std::string::npos ||
                line.find('\t', second + 1) != std::string::npos)
                throw std::runtime_error("expected exactly three TAB-separated fields");
            loaded.add({line.substr(0, first), line.substr(first + 1, second - first - 1),
                        parse_weight(line.substr(second + 1))});
        } catch (const std::exception& e) {
            throw std::runtime_error("base dictionary line " + std::to_string(line_number) + ": " + e.what());
        }
    }
    if (file.bad()) throw std::runtime_error("failed while reading base dictionary");
    if (loaded.size() == 0) throw std::runtime_error("base dictionary has no entries");
    *this = std::move(loaded);
}

void Lexicon::add(Entry entry) {
    validate_text(entry.text);
    entry.pronunciation = normalize_pronunciation(entry.pronunciation);
    if (entry.weight == 0 || entry.weight > max_weight) throw std::invalid_argument("invalid weight");
    const UserKey key{entry.pronunciation, entry.text};
    const auto existing = entry_index_.find(key);
    if (existing != entry_index_.end()) {
        // Independent sources have incomparable counts: keep the larger weight, never sum.
        entries_[existing->second].weight = std::max(entries_[existing->second].weight, entry.weight);
        return;
    }
    std::size_t node = 0;
    std::istringstream stream(entry.pronunciation);
    for (std::string syllable; stream >> syllable;) {
        const auto found = nodes_[node].next.find(syllable);
        if (found == nodes_[node].next.end()) {
            const auto next = nodes_.size();
            nodes_[node].next.emplace(syllable, next);
            nodes_.emplace_back();
            node = next;
        } else node = found->second;
    }
    const auto id = entries_.size();
    if (entry.pronunciation.find(' ') != std::string::npos)
        word_readings_[entry.text].insert(entry.pronunciation);
    entries_.push_back(std::move(entry));
    nodes_[node].entries.push_back(id);
    entry_index_.emplace(key, id);
}

std::vector<Candidate> Lexicon::lookup(const std::string& input, const UserDictionary& users,
                                      std::size_t limit) const {
    const auto query = normalize_query(input);
    std::vector<Match> matches;
    match_prefixes(0, 0, query, matches);
    // One displayed candidate per text, preserving the best matching pronunciation.
    std::map<std::string, Candidate> unique;
    for (const auto& match : matches) {
        if (match.end != query.size()) continue;
        auto candidate = ranked(entries_[match.id], users);
        candidate.input_end = match.end;
        candidate.abbreviations = match.abbreviations;
        candidate.completed = match.completed;
        const auto previous = unique.find(candidate.text);
        if (previous == unique.end() || preferred(candidate, previous->second) ||
            (!preferred(previous->second, candidate) && candidate.pronunciation < previous->second.pronunciation))
            unique[candidate.text] = std::move(candidate);
    }
    std::vector<Candidate> result;
    for (auto& item : unique) result.push_back(std::move(item.second));
    std::sort(result.begin(), result.end(), preferred);
    if (result.size() > limit) result.resize(limit);
    return result;
}

void Lexicon::match_prefixes(std::size_t node, std::size_t offset, const std::string& query,
                            std::vector<Match>& result) const {
    struct State { std::size_t node, offset, abbreviations; };
    std::deque<State> pending{{node, offset, 0}};
    std::set<std::pair<std::size_t, std::size_t>> visited;
    while (!pending.empty()) {
        const auto current = pending.front(); pending.pop_front();
        // Zero-cost full spellings go to the front, shortened spellings to the
        // back. The first visit to a node/offset has the fewest abbreviations.
        if (!visited.emplace(current.node, current.offset).second) continue;
        for (const auto id : nodes_[current.node].entries)
            result.push_back({id, current.offset, current.abbreviations});
        if (current.offset == query.size()) continue;
        const auto& edges = nodes_[current.node].next;
        for (auto it = edges.lower_bound(std::string(1, query[current.offset]));
             it != edges.end() && it->first.front() == query[current.offset]; ++it) {
            const auto remaining = query.size() - current.offset;
            const auto& syllable = it->first;
            // A single letter or zh/ch/sh remains an existing initial, not a
            // second completion route with artificially better precision.
            const bool digraph = remaining == 2 && syllable.size() > 2 && syllable[1] == 'h' &&
                (syllable[0] == 'z' || syllable[0] == 'c' || syllable[0] == 's');
            if (remaining > 1 && remaining < syllable.size() && !digraph &&
                syllable.compare(0, remaining, query, current.offset, remaining) == 0) {
                // Only terminal entries of this one edge: never descend into
                // untyped syllables, or complete a syllable before the tail.
                for (const auto id : nodes_[it->second].entries)
                    result.push_back({id, query.size(), current.abbreviations, true});
            }
            for (const auto length : spelling_lengths(it->first, query, current.offset)) {
                if (!length) continue;
                auto next = current.offset + length;
                if (next < query.size() && query[next] == '\'') ++next;
                const bool shortened = length != it->first.size();
                State state{it->second, next, current.abbreviations + shortened};
                if (shortened) pending.push_back(state);
                else pending.push_front(state);
            }
        }
    }
}

std::vector<Candidate> Lexicon::lookup_composition(const std::string& input,
                                                const UserDictionary& users, std::size_t limit) const {
    const auto query = normalize_query(input);
    std::vector<Match> matches;
    match_prefixes(0, 0, query, matches);
    // Compute once rather than scan the remainder again for every prefix word.
    const auto suffixes = matches.empty() ? std::vector<bool>{} : spellable_suffixes(query);
    std::map<std::pair<std::string, std::size_t>, Candidate> unique;
    for (const auto& match : matches) {
        const auto end = match.end;
        if (!suffixes[end]) continue;
        auto candidate = ranked(entries_[match.id], users);
        candidate.input_end = end;
        candidate.abbreviations = match.abbreviations;
        candidate.completed = match.completed;
        const auto key = std::make_pair(candidate.text, end);
        const auto previous = unique.find(key);
        if (previous == unique.end() || preferred(candidate, previous->second) ||
            (!preferred(previous->second, candidate) && candidate.pronunciation < previous->second.pronunciation))
            unique[key] = std::move(candidate);
    }
    if (limit) {
        for (auto& candidate : decode_normalized(query, users, 5)) {
            if (!candidate.synthesized) continue; // Exact matches are already in the prefix index.
            const auto key = std::make_pair(candidate.text, candidate.input_end);
            if (!unique.count(key)) unique.emplace(key, std::move(candidate));
        }
    }
    std::vector<Candidate> result;
    for (auto& item : unique) result.push_back(std::move(item.second));
    std::sort(result.begin(), result.end(), [](const Candidate& a, const Candidate& b) {
        if (a.input_end != b.input_end) return a.input_end > b.input_end;
        if (a.abbreviations != b.abbreviations) return a.abbreviations < b.abbreviations;
        if (a.synthesized != b.synthesized) return !a.synthesized;
        return preferred(a, b);
    });
    // The same word may match both nihao and the shorter ni+h prefix. Display
    // it once, retaining the longest consumption and then the best reading.
    std::set<std::string> displayed;
    result.erase(std::remove_if(result.begin(), result.end(), [&](const auto& candidate) {
        return !displayed.insert(candidate.text).second;
    }), result.end());
    if (result.size() > limit) result.resize(limit);
    return result;
}

void learn(UserDictionary& users, const Candidate& candidate) {
    validate_text(candidate.text);
    const auto pronunciation = normalize_pronunciation(candidate.pronunciation);
    auto& count = users[{pronunciation, candidate.text}];
    if (count < 1000000000) ++count;
}

} // namespace pinyin
