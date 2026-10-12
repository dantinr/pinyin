#include "pinyin/input_statistics.hpp"
#include "pinyin/user_store.hpp"
#include "statistics_file.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace pinyin {
namespace {
using statistics_file::number;
using statistics_file::validate_date;
using statistics_file::sum;
using statistics_file::snapshot;
void merge(InputTotals& target, const InputTotals& change) {
    // Compute all fields before updating: an overflowing counter cannot apply
    // only part of the transaction.
    const InputTotals next{sum(target.characters, change.characters), sum(target.commits, change.commits),
        sum(target.active_ms, change.active_ms)};
    target = next;
}
void validate(const InputTotals& total) {
    if (!total.characters || !total.commits || total.commits > total.characters)
        throw std::runtime_error("invalid statistics totals");
}
void publish(const std::filesystem::path& path, const InputStatistics& days) {
    std::ostringstream contents;
    contents << "# private-pinyin input statistics v1: date<TAB>characters<TAB>commits<TAB>active_ms\n";
    for (const auto& entry : days)
        contents << entry.first << '\t' << entry.second.characters << '\t' << entry.second.commits << '\t'
            << entry.second.active_ms << '\n';
    const auto bytes = contents.str();
    statistics_file::publish(path, bytes);
}
}
std::filesystem::path input_statistics_path(const std::filesystem::path& user) {
    return user.empty() ? std::filesystem::path{} : user.parent_path() / L"statistics.tsv";
}
std::string local_statistics_date() {
    SYSTEMTIME time{}; GetLocalTime(&time); char date[16]{};
    sprintf_s(date, "%04u-%02u-%02u", time.wYear, time.wMonth, time.wDay);
    return date;
}
std::uint64_t chinese_character_count(const std::string& text) {
    if (text.empty()) return 0;
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()))
        throw std::runtime_error("statistics input is too large");
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (!length) throw std::runtime_error("invalid UTF-8 in statistics input");
    std::wstring value(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), value.data(), length);
    std::uint64_t count = 0;
    for (std::size_t index = 0; index < value.size(); ++index) {
        auto code = static_cast<std::uint32_t>(value[index]);
        if (code >= 0xd800 && code <= 0xdbff && index + 1 < value.size()) {
            const auto low = static_cast<std::uint32_t>(value[++index]);
            code = 0x10000 + ((code - 0xd800) << 10) + (low - 0xdc00);
        }
        if (code == 0x3007 || (code >= 0x3400 && code <= 0x4dbf) || (code >= 0x4e00 && code <= 0x9fff) ||
            (code >= 0xf900 && code <= 0xfaff) || (code >= 0x20000 && code <= 0x2fa1f) ||
            (code >= 0x30000 && code <= 0x323af)) ++count;
    }
    return count;
}
InputStatistics read_input_statistics(const std::filesystem::path& path) {
    if (path.empty()) return {};
    InputStatistics days; std::istringstream input(snapshot(path)); std::size_t row = 0;
    for (std::string line; std::getline(input, line);) {
        ++row;
        if (row == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        if (line.front() == '#') {
            if (line.find("# private-pinyin input statistics v") == 0 &&
                line != "# private-pinyin input statistics v1: date<TAB>characters<TAB>commits<TAB>active_ms")
                throw std::runtime_error("unsupported input statistics format");
            continue;
        }
        try {
            std::array<std::string, 4> fields; std::size_t start = 0;
            for (std::size_t index = 0; index < fields.size(); ++index) {
                const auto end = line.find('\t', start);
                if ((index < 3) == (end == std::string::npos)) throw std::runtime_error("expected four TAB-separated fields");
                fields[index] = line.substr(start, end == std::string::npos ? end : end - start); start = end + 1;
            }
            validate_date(fields[0]);
            const InputTotals totals{number(fields[1]), number(fields[2]), number(fields[3])}; validate(totals);
            if (!days.emplace(fields[0], totals).second) throw std::runtime_error("duplicate statistics date");
        } catch (const std::exception& failure) {
            throw std::runtime_error("input statistics line " + std::to_string(row) + ": " + failure.what());
        }
    }
    return days;
}
InputTotals total_input_statistics(const InputStatistics& days) {
    InputTotals totals;
    for (const auto& entry : days) merge(totals, entry.second);
    return totals;
}
double input_characters_per_minute(const InputTotals& totals) noexcept {
    return totals.active_ms ? static_cast<double>(totals.characters) * 60000.0 / static_cast<double>(totals.active_ms) : 0;
}
void add_input_statistics(const std::filesystem::path& path, const InputStatistics& changes, unsigned timeout) {
    if (changes.empty()) return;
    if (path.empty()) throw std::runtime_error("statistics path is unavailable");
    for (const auto& change : changes) { validate_date(change.first); validate(change.second); }
    // Reuse the existing process-safe transaction lock; do not call its
    // dictionary parser. The statistics snapshot has its own numeric format.
    UserStore transaction(path, timeout);
    auto days = read_input_statistics(transaction.path());
    for (const auto& change : changes) merge(days[change.first], change.second);
    publish(transaction.path(), days);
}
void InputActivityClock::activity(std::uint64_t now) noexcept {
    if (last_ && now >= *last_) {
        const auto interval = now - *last_;
        if (interval < 30000 && interval <= std::numeric_limits<std::uint64_t>::max() - elapsed_) elapsed_ += interval;
    }
    last_ = now;
}
void InputStatisticsRecorder::record(const std::string& text, std::uint64_t active_ms, const std::string& date) noexcept {
    if (path_.empty()) return;
    try {
        const auto count = chinese_character_count(text);
        if (!count) return;
        validate_date(date);
        merge(pending_[date], {count, 1, active_ms});
        flush();
    } catch (...) {}
}
bool InputStatisticsRecorder::flush() noexcept {
    if (pending_.empty()) return true;
    try { add_input_statistics(path_, pending_); pending_.clear(); return true; }
    catch (...) { return false; }
}
}
