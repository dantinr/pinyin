#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pinyin {
struct VocabularyGrowth {
    std::string since;
    std::map<std::string, std::uint64_t> days;
    bool operator==(const VocabularyGrowth& other) const noexcept {
        return since == other.since && days == other.days;
    }
    bool operator!=(const VocabularyGrowth& other) const noexcept { return !(*this == other); }
};
struct VocabularyGrowthPoint {
    std::string date;
    // Missing means the date predates tracking, rather than zero additions.
    std::optional<std::uint64_t> added;
};
std::filesystem::path vocabulary_growth_path(const std::filesystem::path& user);
VocabularyGrowth read_vocabulary_growth(const std::filesystem::path& path);
// Passing zero initializes tracking without claiming any historical additions.
void add_vocabulary_growth(const std::filesystem::path& path, const std::string& date,
    std::uint64_t added, unsigned lock_timeout_ms = 100);
std::uint64_t total_vocabulary_growth(const VocabularyGrowth& growth);
std::vector<VocabularyGrowthPoint> vocabulary_growth_series(const VocabularyGrowth& growth,
    const std::string& ending, unsigned length = 30);
}
