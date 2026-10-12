#include "tsf_text_store.hpp"
#include "test_workspace.hpp"
#include "pinyin/input_statistics.hpp"
#include "pinyin/vocabulary_growth.hpp"
#include "pinyin/ime_settings.hpp"
#include <iostream>
#include <fstream>

namespace {
int checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
void choose(pinyin::testing::Harness& ime, const wchar_t* expected) {
    using namespace pinyin::testing;
    ComPtr<ITfUIElementMgr> manager; Harness::require(ime.manager.As(&manager), "get candidate manager");
    ComPtr<IEnumTfUIElements> elements; Harness::require(manager->EnumUIElements(&elements), "enumerate candidates");
    for (;;) {
        ComPtr<ITfUIElement> element; ULONG fetched = 0;
        if (elements->Next(1, &element, &fetched) != S_OK || !fetched) break;
        GUID id{}; element->GetGUID(&id); if (id != candidate_id) continue;
        ComPtr<ITfCandidateListUIElement> candidates;
        Harness::require(element.As(&candidates), "get candidate list");
        UINT count = 0; candidates->GetCount(&count);
        for (UINT index = 0; index < count; ++index) {
            BSTR text = nullptr; Harness::require(candidates->GetString(index, &text), "read candidate");
            const auto match = std::wstring(text, SysStringLen(text)) == expected; SysFreeString(text);
            if (!match) continue;
            UINT selected = 0; candidates->GetSelection(&selected);
            while (selected < index) { check(ime.key(VK_DOWN), "navigate candidate down"); ++selected; }
            while (selected > index) { check(ime.key(VK_UP), "navigate candidate up"); --selected; }
            check(ime.key(VK_SPACE), "confirm candidate"); return;
        }
    }
    throw std::runtime_error("candidate not found");
}
}
int wmain(int argc, wchar_t** argv) {
    using namespace pinyin; using namespace pinyin::testing;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(argc == 2 && SUCCEEDED(initialized), "cannot initialize statistics TSF test");
        TestWorkspace workspace;
        const auto user = default_user_path(); const auto path = input_statistics_path(user);
        {
            Harness ime(argv[1]);
            BOOL eaten = FALSE;
            for (int index = 0; index < 3; ++index) ime.keys->OnTestKeyDown(ime.context.Get(), 'N', 0, &eaten);
            check(!std::filesystem::exists(path), "key probing created input statistics");
            ime.type("nihao"); ime.key(VK_SPACE);
            check(ime.store->text == L"你好" && total_input_statistics(read_input_statistics(path)).characters == 2,
                "successful candidate commit was not counted");
            check(total_vocabulary_growth(read_vocabulary_growth(vocabulary_growth_path(user))) == 1,
                "first successful learning did not count a new word");
            ime.type("nihao"); ime.key(VK_ESCAPE);
            ime.type("nihao"); ime.key(VK_RETURN);
            check(total_input_statistics(read_input_statistics(path)).characters == 2, "cancelled or raw input was counted");
            ime.type("nihao"); ime.key(VK_OEM_COMMA);
            const auto punctuation = total_input_statistics(read_input_statistics(path));
            check(punctuation.characters == 4 && punctuation.commits == 2, "punctuation commit was missed or punctuation was counted");
            check(total_vocabulary_growth(read_vocabulary_growth(vocabulary_growth_path(user))) == 1,
                "repeated, cancelled or literal input duplicated new words");
            set_ime_learning(user, false);
            ime.type("nihao"); ime.key(VK_SPACE);
            check(total_input_statistics(read_input_statistics(path)).characters == 6, "learning opt-out disabled numerical statistics");
            ime.type("nihao"); ime.store->reject_writes = true;
            check(!ime.key(VK_SPACE) && total_input_statistics(read_input_statistics(path)).characters == 6,
                "rejected host commit was counted");
            ime.store->reject_writes = false; ime.key(VK_ESCAPE);
            ime.type("nihao");
            ime.service->Deactivate(); Harness::pump();
            check(total_input_statistics(read_input_statistics(path)).characters == 6, "focus/deactivation preservation was counted as confirmation");
            ime.close(); check(ime.unload_result == S_OK, "statistics retained TSF DLL references");
        }
        {
            Harness ime(argv[1]); ime.type("nihao"); ime.key(VK_SPACE);
            check(total_input_statistics(read_input_statistics(path)).characters == 8, "restart lost earlier input statistics");
            ime.type("hello"); ime.key(VK_SPACE); ime.key(VK_RETURN);
            check(total_input_statistics(read_input_statistics(path)).characters == 8, "English literal commit was counted as Chinese");
            ime.close(); check(ime.unload_result == S_OK, "second host leaked statistics references");
        }
        for (auto scope : {IS_PASSWORD, IS_NUMERIC_PIN}) {
            Harness ime(argv[1], scope);
            check(!ime.key('N') && !ime.key('I') && !ime.key(VK_SPACE) &&
                total_input_statistics(read_input_statistics(path)).characters == 8, "password/PIN input was intercepted or counted");
            ime.close(); check(ime.unload_result == S_OK, "sensitive host leaked references");
        }
        {
            set_ime_learning(user, true);
            Harness ime(argv[1]); ime.type("ruhe"); choose(ime, L"如");
            check(ime.compositions() == 1 && total_input_statistics(read_input_statistics(path)).characters == 8,
                "partial candidate confirmation counted unfinished input");
            choose(ime, L"荷");
            check(ime.store->text == L"如荷" && total_input_statistics(read_input_statistics(path)).characters == 10,
                "candidate UI completion missed or duplicated the segmented word");
            check(total_vocabulary_growth(read_vocabulary_growth(vocabulary_growth_path(user))) == 2,
                "segmented confirmation did not add exactly one complete new word");
            ime.close(); check(ime.unload_result == S_OK, "candidate statistics leaked references");
        }
        // Storage failures must leave ordinary typing functional.
        { std::ofstream broken(path, std::ios::binary | std::ios::trunc); broken << "invalid statistics\n"; }
        {
            Harness ime(argv[1]); ime.type("nihao"); ime.key(VK_SPACE);
            check(ime.store->text == L"你好", "statistics error prevented text from reaching host");
            ime.close(); check(ime.unload_result == S_OK, "failed statistics storage leaked references");
        }
        CoUninitialize(); std::cout << "PASS: " << checks << " real TSF statistics checks\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        if (SUCCEEDED(initialized)) CoUninitialize(); return 1;
    }
}
