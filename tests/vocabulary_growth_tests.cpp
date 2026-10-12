#include "pinyin/vocabulary_growth.hpp"
#include "pinyin/input_statistics.hpp"
#include "pinyin/learning_dictionary.hpp"
#include "test_workspace.hpp"
#include <fstream>
#include <iostream>
#include <limits>

namespace {
int checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class F> void rejects(F operation, const char* message) {
    bool failed = false; try { operation(); } catch (const std::exception&) { failed = true; }
    check(failed, message);
}
std::string bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
void write(const std::filesystem::path& path, const std::string& contents) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!(file << contents)) throw std::runtime_error("cannot write growth fixture");
}
}
int wmain(int argc, wchar_t** argv) {
    using namespace pinyin;
    try {
        if (argc == 4 && std::wstring(argv[1]) == L"--writer") {
            const auto text = "并发新词" + std::string(1, static_cast<char>(*argv[3]));
            for (int index = 0; index < 12; ++index) {
                UserStore store(argv[2], 1000); auto words = store.load();
                ++words[{"gong you xin ci", "共有新词"}]; ++words[{"bing fa xin ci", text}];
                store.save(words);
            }
            return 0;
        }
        testing::TestWorkspace workspace;
        const auto user = workspace.root / L"words.user.tsv";
        const auto path = vocabulary_growth_path(user);
        const auto date = local_statistics_date();
        check(vocabulary_growth_path({}).empty(), "empty user path invented storage");
        check(read_vocabulary_growth(path).since.empty() && !std::filesystem::exists(path), "read created tracking history");
        auto points = vocabulary_growth_series({}, "2026-01-02", 4);
        check(points.front().date == "2025-12-30" && !points.back().added, "untracked dates were displayed as zero");
        // Pre-existing words and automatic legacy migrations have unknown dates.
        { UserStore store(user); store.save({{{"xing", "行"}, 2}}, false); }
        check(!std::filesystem::exists(path), "historical baseline became today's additions");
        add_vocabulary_growth(path, date, 0);
        auto growth = read_vocabulary_growth(path);
        check(growth.since == date && growth.days.empty(), "tracking initialization invented vocabulary");
        {
            UserStore store(user); auto words = store.load();
            ++words[{"xing", "行"}]; words[{"hang", "行"}] = 1; store.save(words);
        }
        check(read_vocabulary_growth(path).days.empty(), "reselection or another reading counted as a new text");
        const auto base = workspace.root / L"base.tsv";
        write(base, "如\tru\t10\n何\the\t10\n");
        LearningDictionary first(base, user), second(base, user);
        check(first.remember({InputAction::commit, "如何", "ru he"}) &&
            second.remember({InputAction::commit, "如何", "ru he"}), "learning fixture failed");
        check(total_vocabulary_growth(read_vocabulary_growth(path)) == 1, "independent application caches duplicated new words");
        const auto before = bytes(path);
        check(!first.remember({InputAction::update, "新词", "xin ci"}), "partial text was learned");
        set_ime_learning(user, false);
        check(!first.remember({InputAction::commit, "新词", "xin ci"}) && bytes(path) == before,
            "disabled or unfinished learning changed additions");
        set_ime_learning(user, true);
        const auto incoming = workspace.root / L"import.tsv";
        write(incoming, "行\thang\t8\n你好\tni hao\t3\n你好\tni hao a\t2\n");
        auto merged = merge_user_dictionary(user, incoming);
        check(merged.added == 2 && total_vocabulary_growth(read_vocabulary_growth(path)) == 2,
            "personal merge counted readings instead of distinct new texts");
        merge_user_dictionary(user, incoming);
        check(total_vocabulary_growth(read_vocabulary_growth(path)) == 2, "reimport duplicated additions");
        write(incoming, "旧词\tjiu ci\t2\n");
        merge_user_dictionary(user, incoming, false);
        check(total_vocabulary_growth(read_vocabulary_growth(path)) == 2, "legacy migration was counted as new learning");
        const auto snapshot = bytes(path);
        const auto pinned = CreateFileW(user.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(pinned != INVALID_HANDLE_VALUE, "cannot pin dictionary fixture");
        const auto remembered = first.remember({InputAction::commit, "新词", "xin ci"});
        CloseHandle(pinned);
        check(!remembered && bytes(path) == snapshot, "failed dictionary publication produced growth");
        // Calendar gaps are zero only after tracking starts; no historical guess.
        const auto calendar = workspace.root / L"calendar.tsv";
        add_vocabulary_growth(calendar, "2024-02-28", 2);
        add_vocabulary_growth(calendar, "2024-03-01", 5);
        growth = read_vocabulary_growth(calendar);
        points = vocabulary_growth_series(growth, "2024-03-02", 5);
        check(points[0].date == "2024-02-27" && !points[0].added && points[1].added == 2 &&
            points[2].date == "2024-02-29" && points[2].added == 0 && points[3].added == 5 && points[4].added == 0,
            "leap-day gaps or tracking boundary were plotted incorrectly");
        add_vocabulary_growth(calendar, "2024-02-27", 1); // Computer date can move backwards.
        check(read_vocabulary_growth(calendar).since == "2024-02-27", "backward local date made history unreadable");
        const auto valid = bytes(calendar);
        rejects([&] { add_vocabulary_growth(calendar, "2023-02-29", 1); }, "invalid calendar date was accepted");
        check(bytes(calendar) == valid, "invalid date damaged growth history");
        for (const auto* broken : {"", "# private-pinyin vocabulary growth v2\n", "2026-10-12\t1\n",
            "# private-pinyin vocabulary growth v1: date<TAB>new_words\n# tracking-start: 2026-10-12\n2026-10-12\t0\n",
            "# private-pinyin vocabulary growth v1: date<TAB>new_words\n# tracking-start: 2026-10-12\n2026-10-12\t1\n2026-10-12\t2\n",
            "# private-pinyin vocabulary growth v1: date<TAB>new_words\n# tracking-start: 2026-10-12\n2026-10-11\t1\n"}) {
            write(calendar, broken);
            rejects([&] { add_vocabulary_growth(calendar, date, 1); }, "bad growth data was overwritten");
            check(bytes(calendar) == broken, "unreadable growth data was destroyed");
        }
        write(path, "broken growth\n");
        check(first.remember({InputAction::commit, "新词", "xin ci"}) && bytes(path) == "broken growth\n",
            "growth failure prevented learning or overwrote damaged data");
        const auto overflow = workspace.root / L"overflow.tsv";
        add_vocabulary_growth(overflow, date, std::numeric_limits<std::uint64_t>::max());
        const auto full = bytes(overflow);
        rejects([&] { add_vocabulary_growth(overflow, date, 1); }, "growth counter overflow was accepted");
        check(bytes(overflow) == full, "overflow partially updated growth");
        // Real processes add a shared word plus their own word repeatedly.
        const auto concurrent_user = workspace.root / L"concurrent" / L"words.user.tsv";
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        std::vector<HANDLE> processes;
        for (unsigned index = 0; index < 3; ++index) {
            auto command = L"\"" + std::wstring(executable) + L"\" --writer \"" + concurrent_user.wstring() + L"\" " +
                std::to_wstring(index);
            STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
            check(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                nullptr, nullptr, &startup, &child) != 0, "cannot launch vocabulary writer");
            CloseHandle(child.hThread); processes.push_back(child.hProcess);
        }
        for (const auto process : processes) {
            const auto waited = WaitForSingleObject(process, 20000); DWORD result = 1;
            GetExitCodeProcess(process, &result); CloseHandle(process);
            check(waited == WAIT_OBJECT_0 && result == 0, "concurrent learning writer failed");
        }
        growth = read_vocabulary_growth(vocabulary_growth_path(concurrent_user));
        check(total_vocabulary_growth(growth) == 4 && read_user_dictionary(concurrent_user).size() == 4,
            "multi-process growth lost additions or counted repeated use");
        check(bytes(vocabulary_growth_path(concurrent_user)).find("新词") == std::string::npos,
            "growth statistics stored personal text");
        std::cout << "PASS: " << checks << " vocabulary growth checks\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1;
    }
}
