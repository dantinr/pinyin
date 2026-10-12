#include "pinyin/vocabulary_growth.hpp"
#include "pinyin/user_store.hpp"
#include "statistics_file.hpp"
#include <cstdio>
#include <sstream>

namespace pinyin {
namespace {
constexpr auto header = "# private-pinyin vocabulary growth v1: date<TAB>new_words";
constexpr auto start_prefix = "# tracking-start: ";
std::string offset_date(const std::string& date, int offset) {
    statistics_file::validate_date(date);
    SYSTEMTIME time{};
    time.wYear = static_cast<WORD>(std::stoul(date.substr(0, 4)));
    time.wMonth = static_cast<WORD>(std::stoul(date.substr(5, 2)));
    time.wDay = static_cast<WORD>(std::stoul(date.substr(8, 2)));
    FILETIME value{};
    if (!SystemTimeToFileTime(&time, &value)) throw std::runtime_error("cannot convert growth date");
    ULARGE_INTEGER ticks{}; ticks.LowPart = value.dwLowDateTime; ticks.HighPart = value.dwHighDateTime;
    constexpr std::uint64_t day_ticks = 864000000000ULL;
    const auto difference = static_cast<std::int64_t>(offset) * static_cast<std::int64_t>(day_ticks);
    if (difference < 0 && ticks.QuadPart < static_cast<std::uint64_t>(-difference))
        throw std::runtime_error("growth date before supported calendar");
    ticks.QuadPart = difference < 0 ? ticks.QuadPart - static_cast<std::uint64_t>(-difference) :
        ticks.QuadPart + static_cast<std::uint64_t>(difference);
    value.dwLowDateTime = ticks.LowPart; value.dwHighDateTime = ticks.HighPart;
    if (!FileTimeToSystemTime(&value, &time) || time.wYear > 9999)
        throw std::runtime_error("growth date outside supported calendar");
    char result[16]{}; sprintf_s(result, "%04u-%02u-%02u", time.wYear, time.wMonth, time.wDay);
    return result;
}
}
std::filesystem::path vocabulary_growth_path(const std::filesystem::path& user) {
    return user.empty() ? std::filesystem::path{} : user.parent_path() / L"vocabulary-growth.tsv";
}
VocabularyGrowth read_vocabulary_growth(const std::filesystem::path& path) {
    if (path.empty()) return {};
    const auto bytes = statistics_file::snapshot(path);
    if (bytes.empty()) {
        if (std::filesystem::exists(path)) throw std::runtime_error("empty vocabulary growth file");
        return {};
    }
    VocabularyGrowth growth; bool recognized = false; std::size_t row = 0;
    std::istringstream input(bytes);
    for (std::string line; std::getline(input, line);) {
        ++row;
        if (row == 1 && line.compare(0, 3, "\xef\xbb\xbf") == 0) line.erase(0, 3);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) continue;
        try {
            if (line.find("# private-pinyin vocabulary growth v") == 0) {
                if (line != header || recognized) throw std::runtime_error("unsupported or duplicate growth header");
                recognized = true; continue;
            }
            if (line.find(start_prefix) == 0) {
                if (!growth.since.empty()) throw std::runtime_error("duplicate tracking start");
                growth.since = line.substr(std::char_traits<char>::length(start_prefix));
                statistics_file::validate_date(growth.since); continue;
            }
            if (line.front() == '#') continue;
            const auto tab = line.find('\t');
            if (tab == std::string::npos || line.find('\t', tab + 1) != std::string::npos)
                throw std::runtime_error("expected two TAB-separated fields");
            const auto date = line.substr(0, tab);
            statistics_file::validate_date(date);
            const auto added = statistics_file::number(line.substr(tab + 1));
            if (!added || !growth.days.emplace(date, added).second)
                throw std::runtime_error("zero additions or duplicate growth date");
        } catch (const std::exception& error) {
            throw std::runtime_error("vocabulary growth line " + std::to_string(row) + ": " + error.what());
        }
    }
    if (!recognized || growth.since.empty()) throw std::runtime_error("missing growth format or tracking start");
    if (!growth.days.empty() && growth.days.begin()->first < growth.since)
        throw std::runtime_error("growth date predates tracking start");
    total_vocabulary_growth(growth); // Reject aggregate overflow as well as row overflow.
    return growth;
}
void add_vocabulary_growth(const std::filesystem::path& path, const std::string& date,
    std::uint64_t added, unsigned timeout) {
    if (path.empty()) throw std::runtime_error("vocabulary growth path is unavailable");
    statistics_file::validate_date(date);
    UserStore transaction(path, timeout); // Lock only; this is not a dictionary file.
    auto growth = read_vocabulary_growth(transaction.path());
    const auto previous = growth;
    if (growth.since.empty() || date < growth.since) growth.since = date;
    if (added) growth.days[date] = statistics_file::sum(growth.days[date], added);
    total_vocabulary_growth(growth);
    if (growth == previous) return;
    std::ostringstream output; output << header << '\n' << start_prefix << growth.since << '\n';
    for (const auto& day : growth.days) output << day.first << '\t' << day.second << '\n';
    statistics_file::publish(transaction.path(), output.str());
}
std::uint64_t total_vocabulary_growth(const VocabularyGrowth& growth) {
    std::uint64_t total = 0;
    for (const auto& day : growth.days) total = statistics_file::sum(total, day.second);
    return total;
}
std::vector<VocabularyGrowthPoint> vocabulary_growth_series(const VocabularyGrowth& growth,
    const std::string& ending, unsigned length) {
    if (!length || length > 366) throw std::runtime_error("invalid growth series length");
    if (!growth.since.empty()) statistics_file::validate_date(growth.since);
    std::vector<VocabularyGrowthPoint> result; result.reserve(length);
    for (unsigned index = 0; index < length; ++index) {
        const auto date = offset_date(ending, static_cast<int>(index) - static_cast<int>(length) + 1);
        std::optional<std::uint64_t> value;
        if (!growth.since.empty() && date >= growth.since) {
            const auto found = growth.days.find(date); value = found == growth.days.end() ? 0 : found->second;
        }
        result.push_back({date, value});
    }
    return result;
}
}
