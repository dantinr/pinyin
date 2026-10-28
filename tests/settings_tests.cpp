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
        check(state.learning && state.chinese_punctuation && state.automatic_english && !state.rotate_icons && !std::filesystem::exists(path.parent_path()),
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
        set_icon_rotation(path, true);
        state = read_ime_settings(path);
        check(state.rotate_icons && !state.learning && state.chinese_punctuation && state.automatic_english,
            "icon opt-in was not saved independently");
        set_icon_rotation(path, false);
        check(!read_ime_settings(path).rotate_icons && !std::filesystem::exists(icon_rotation_flag(path)),
            "icon opt-out left a marker or remained enabled");
        std::exception_ptr first_error, second_error, third_error;
        std::thread first([&] { try { for (int i = 0; i < 40; ++i) set_ime_learning(path, i % 2 == 0); } catch (...) { first_error = std::current_exception(); } });
        std::thread second([&] { try { for (int i = 0; i < 40; ++i) set_chinese_punctuation(path, i % 2 == 0); } catch (...) { second_error = std::current_exception(); } });
        std::thread third([&] { try { for (int i = 0; i < 40; ++i) set_automatic_english(path, i % 2 == 0); } catch (...) { third_error = std::current_exception(); } });
        first.join(); second.join(); third.join();
        if (first_error) std::rethrow_exception(first_error); if (second_error) std::rethrow_exception(second_error);
        if (third_error) std::rethrow_exception(third_error);
        state = read_ime_settings(path); check(!state.learning && !state.chinese_punctuation && !state.automatic_english,
            "concurrent independent changes lost an opt-out");
        check(!read_candidate_colors(path).custom && !read_candidate_colors({}).custom,
            "missing candidate colors enabled customization");
        set_candidate_colors(path, {true, 0x281e18, 0x42c3f5});
        auto colors = read_candidate_colors(path);
        check(colors.custom && colors.background == 0x281e18 && colors.selection == 0x42c3f5,
            "candidate colors did not persist");
        check(!read_ime_settings(path).learning && !std::filesystem::exists(path),
            "candidate customization changed learning or created personal words");
        bool invalid_color = false;
        try { set_candidate_colors(path, {true, 0x1000000, 0}); } catch (...) { invalid_color = true; }
        check(invalid_color && read_candidate_colors(path).background == colors.background,
            "invalid candidate color overwrote saved settings");
        for (const auto* invalid : {"v1 abcdef 000000", "v2 abcdef 000000\n", "v1 zzzzzz 000000\n", "v1 ffffff 000000\ntrailing"}) {
            { std::ofstream damaged(candidate_colors_path(path), std::ios::binary); damaged << invalid; }
            check(!read_candidate_colors(path).custom, "damaged candidate colors did not fall back safely");
        }
        set_candidate_colors(path, colors);
        set_candidate_colors(path, {});
        check(!read_candidate_colors(path).custom && !std::filesystem::exists(candidate_colors_path(path)) &&
            !read_ime_settings(path).learning, "restoring system colors changed other settings");
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
