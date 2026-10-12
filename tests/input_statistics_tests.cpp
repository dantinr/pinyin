#include "pinyin/input_statistics.hpp"
#include "pinyin/user_store.hpp"
#include "pinyin/user_data.hpp"
#include "test_workspace.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <vector>

namespace {
int checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
template<class Action> void rejects(Action action, const char* message) {
    bool rejected = false; try { action(); } catch (const std::exception&) { rejected = true; } check(rejected, message);
}
std::string bytes(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary); return {std::istreambuf_iterator<char>(file), {}};
}
void write(const std::filesystem::path& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc); if (!(file << content)) throw std::runtime_error("cannot write fixture");
}
}
int wmain(int argc, wchar_t** argv) {
    using namespace pinyin;
    try {
        if (argc == 3 && std::wstring(argv[1]) == L"--writer") {
            for (unsigned index = 0; index < 40; ++index)
                add_input_statistics(argv[2], {{"2026-10-12", {2, 1, 1000}}}, 1000);
            return 0;
        }
        pinyin::testing::TestWorkspace workspace;
        const auto path = input_statistics_path(default_user_path());
        check(input_statistics_path({}).empty() && path.filename() == L"statistics.tsv", "unexpected statistics location");
        check(read_input_statistics(path).empty() && !std::filesystem::exists(path.parent_path()),
            "reading empty statistics created storage");
        check(chinese_character_count("中文，hello 123😀𠀀〇") == 4, "character counts included English, punctuation or surrogate halves");
        check(chinese_character_count("hello world!") == 0, "English was counted as Chinese");
        rejects([] { chinese_character_count("\xff"); }, "invalid UTF-8 was counted");
        InputActivityClock clock;
        clock.activity(1000); clock.activity(2000); clock.activity(32000); clock.activity(33000);
        check(clock.elapsed_ms() == 2000, "long idle interval was counted as typing time");
        clock.reset(); clock.activity(10); clock.activity(110);
        check(clock.elapsed_ms() == 100, "composition cancellation retained earlier typing time");
        add_input_statistics(path, {{"2026-10-11", {6, 2, 6000}}, {"2026-10-12", {2, 1, 2000}}});
        add_input_statistics(path, {{"2026-10-12", {3, 1, 3000}}});
        auto days = read_input_statistics(path);
        check(days.size() == 2 && days.at("2026-10-12") == InputTotals{5, 2, 5000}, "daily changes were lost or assigned to the wrong day");
        check(total_input_statistics(days) == InputTotals{11, 4, 11000}, "cumulative totals disagree with daily totals");
        check(std::abs(input_characters_per_minute(total_input_statistics(days)) - 60.0) < 0.01 &&
            input_characters_per_minute({}) == 0, "speed used the wrong units or divided by zero");
        InputStatisticsRecorder recorder(path);
        {
            UserStore held(path);
            recorder.record("你好", 1000, "2026-10-12");
            check(recorder.has_pending() && !recorder.flush() && read_input_statistics(path) == days,
                "contended writer lost its delta or changed the published file");
        }
        check(recorder.flush() && !recorder.has_pending(), "pending counts were not retried after writer released its lock");
        check(recorder.flush() && read_input_statistics(path).at("2026-10-12") == InputTotals{7, 3, 6000},
            "successful retry was counted more than once");
        recorder.record("hello", 1000, "2026-10-12");
        check(read_input_statistics(path).at("2026-10-12").characters == 7, "English-only commit created a statistics event");
        const auto valid = bytes(path);
        for (const auto& invalid : {"2026-02-29\t2\t1\t1000\n", "2026-10-12\t2\t0\t1000\n",
            "2026-10-12\t2\t3\t1000\n", "2026-10-12\t2\t1\t-1\n", "2026-10-12\t2\t1\t0\textra\n",
            "2026-10-12\t2\t1\t0\n2026-10-12\t3\t1\t0\n", "# private-pinyin input statistics v2\n"}) {
            write(path, invalid);
            rejects([&] { add_input_statistics(path, {{"2026-10-12", {1, 1, 100}}}); }, "invalid live statistics were overwritten");
            check(bytes(path) == invalid, "rejected transaction modified statistics");
        }
        write(path, valid);
        rejects([&] { add_input_statistics(path, {{"2026-13-01", {1, 1, 0}}}); }, "invalid change date was accepted");
        check(bytes(path) == valid, "bad input changed published statistics");
        const auto pinned = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(pinned != INVALID_HANDLE_VALUE, "cannot pin failure fixture");
        try {
            rejects([&] { add_input_statistics(path, {{"2026-10-12", {1, 1, 0}}}); }, "failed replacement reported success");
            check(bytes(path) == valid, "failed replacement damaged published statistics");
        } catch (...) { CloseHandle(pinned); throw; }
        CloseHandle(pinned);
        // Separate processes reproduce real multi-application writers, including
        // interleaved reread/merge/publication of the same day's counters.
        wchar_t executable[32768]{}; GetModuleFileNameW(nullptr, executable, static_cast<DWORD>(std::size(executable)));
        std::vector<HANDLE> processes;
        for (unsigned index = 0; index < 3; ++index) {
            std::wstring command = L"\"" + std::wstring(executable) + L"\" --writer \"" + path.wstring() + L"\"";
            STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION child{};
            check(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                nullptr, nullptr, &startup, &child) != 0, "cannot launch concurrent statistics writer");
            CloseHandle(child.hThread); processes.push_back(child.hProcess);
        }
        for (auto process : processes) {
            const auto waited = WaitForSingleObject(process, 20000); DWORD result = 1;
            GetExitCodeProcess(process, &result); CloseHandle(process);
            check(waited == WAIT_OBJECT_0 && result == 0, "concurrent statistics writer failed");
        }
        days = read_input_statistics(path);
        check(days.at("2026-10-12") == InputTotals{247, 123, 126000}, "multi-process writers lost counts");
        check(bytes(path).find("你好") == std::string::npos && bytes(path).find("hello") == std::string::npos,
            "statistics persisted input text");
        add_input_statistics(path, {{"2024-02-29", {1, 1, 0}}});
        check(read_input_statistics(path).count("2024-02-29") == 1, "valid leap day was rejected");
        write(path, "2026-10-12\t18446744073709551615\t1\t0\n");
        rejects([&] { add_input_statistics(path, {{"2026-10-12", {1, 1, 0}}}); }, "counter overflow wrapped published totals");
        check(bytes(path) == "2026-10-12\t18446744073709551615\t1\t0\n", "overflow changed published data");
        std::cout << "PASS: " << checks << " input statistics checks\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n'; return 1; }
}
