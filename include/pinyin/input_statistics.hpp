#pragma once
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <utility>

namespace pinyin {
struct InputTotals {
    std::uint64_t characters = 0;
    std::uint64_t commits = 0;
    std::uint64_t active_ms = 0;
    bool operator==(const InputTotals& other) const noexcept {
        return characters == other.characters && commits == other.commits && active_ms == other.active_ms;
    }
    bool operator!=(const InputTotals& other) const noexcept { return !(*this == other); }
};
using InputStatistics = std::map<std::string, InputTotals>;

std::filesystem::path input_statistics_path(const std::filesystem::path& user);
std::string local_statistics_date();
std::uint64_t chinese_character_count(const std::string& text);
InputStatistics read_input_statistics(const std::filesystem::path& path);
InputTotals total_input_statistics(const InputStatistics& days);
double input_characters_per_minute(const InputTotals& totals) noexcept;
// One locked read/merge/atomic publication, shared across application processes.
// Invalid existing data is never replaced with an empty snapshot.
void add_input_statistics(const std::filesystem::path& path, const InputStatistics& changes,
    unsigned lock_timeout_ms = 100);

// Uses monotonic time. Ignore pauses of 30 seconds or more, rather than counting
// time spent away from the keyboard as typing time. Holds no input text.
class InputActivityClock {
    std::optional<std::uint64_t> last_;
    std::uint64_t elapsed_ = 0;
public:
    void activity(std::uint64_t now) noexcept;
    void reset() noexcept { last_.reset(); elapsed_ = 0; }
    std::uint64_t elapsed_ms() const noexcept { return elapsed_; }
};

class InputStatisticsRecorder {
    std::filesystem::path path_;
    InputStatistics pending_;
public:
    explicit InputStatisticsRecorder(std::filesystem::path path) : path_(std::move(path)) {}
    // Text is counted immediately and not retained. Failures never reach TSF.
    // Keep unsaved numerical deltas and retry on subsequent commits/deactivation.
    void record(const std::string& text, std::uint64_t active_ms, const std::string& date) noexcept;
    bool flush() noexcept;
    bool has_pending() const noexcept { return !pending_.empty(); }
};
}
