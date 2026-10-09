#include "pinyin/ime_settings.hpp"
#include "test_workspace.hpp"
#include <fstream>
#include <iostream>
#include <thread>

namespace {
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
}
int main() {
    using namespace pinyin;
    try {
        testing::TestWorkspace workspace; const auto path = default_user_path();
        auto state = read_ime_settings(path);
        check(state.learning && state.chinese_punctuation && state.automatic_english && !std::filesystem::exists(path.parent_path()),
            "default settings changed or reading created files");
        state = read_ime_settings({}); check(!state.learning && !state.chinese_punctuation && !state.automatic_english, "missing storage enabled settings");
        set_automatic_english(path, false);
        set_ime_learning(path, false); set_chinese_punctuation(path, false);
        state = read_ime_settings(path); check(!state.learning && !state.chinese_punctuation, "opt-outs did not persist");
        set_ime_learning(path, true);
        state = read_ime_settings(path); check(state.learning && !state.chinese_punctuation, "learning overwrote punctuation setting");
        set_ime_learning(path, false); set_chinese_punctuation(path, true);
        state = read_ime_settings(path); check(!state.learning && state.chinese_punctuation && !state.automatic_english,
            "punctuation overwrote another setting");
        set_automatic_english(path, true);
        state = read_ime_settings(path); check(!state.learning && state.chinese_punctuation && state.automatic_english,
            "automatic English overwrote another setting");
        check(!std::filesystem::exists(path), "setting changes created a personal dictionary");
        std::exception_ptr first_error, second_error, third_error;
        std::thread first([&] { try { for (int i = 0; i < 40; ++i) set_ime_learning(path, i % 2 == 0); } catch (...) { first_error = std::current_exception(); } });
        std::thread second([&] { try { for (int i = 0; i < 40; ++i) set_chinese_punctuation(path, i % 2 == 0); } catch (...) { second_error = std::current_exception(); } });
        std::thread third([&] { try { for (int i = 0; i < 40; ++i) set_automatic_english(path, i % 2 == 0); } catch (...) { third_error = std::current_exception(); } });
        first.join(); second.join(); third.join();
        if (first_error) std::rethrow_exception(first_error); if (second_error) std::rethrow_exception(second_error);
        if (third_error) std::rethrow_exception(third_error);
        state = read_ime_settings(path); check(!state.learning && !state.chinese_punctuation && !state.automatic_english,
            "concurrent independent changes lost an opt-out");
        std::ofstream(workspace.root / L"blocked") << "sentinel";
        bool rejected = false;
        try { set_chinese_punctuation(workspace.root / L"blocked" / L"words.tsv", false); } catch (...) { rejected = true; }
        check(rejected, "unwritable setting falsely reported success");
        rejected = false; try { set_chinese_punctuation({}, false); } catch (...) { rejected = true; }
        check(rejected, "missing settings path falsely reported success");
        rejected = false; try { set_automatic_english({}, false); } catch (...) { rejected = true; }
        check(rejected, "missing English setting path falsely reported success");
        std::cout << "PASS: " << checks << " persistent settings checks\n"; return 0;
    } catch (const std::exception& error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
