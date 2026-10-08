#include "tsf_text_store.hpp"
#include "test_workspace.hpp"
#include "pinyin/learning_dictionary.hpp"
#include <iostream>

namespace {
void choose(pinyin::testing::Harness& ime, const wchar_t* text) {
    using namespace pinyin::testing;
    ComPtr<ITfUIElementMgr> ui; Harness::require(ime.manager.As(&ui), "get UI manager");
    ComPtr<IEnumTfUIElements> enumeration; Harness::require(ui->EnumUIElements(&enumeration), "enumerate UI");
    for (;;) {
        ComPtr<ITfUIElement> element; ULONG fetched = 0;
        if (enumeration->Next(1, &element, &fetched) != S_OK || !fetched) break;
        GUID id{}; element->GetGUID(&id);
        if (id != candidate_id) continue;
        ComPtr<ITfCandidateListUIElement> candidates; Harness::require(element.As(&candidates), "get candidates");
        UINT count = 0; candidates->GetCount(&count);
        for (UINT i = 0; i < count; ++i) {
            BSTR value = nullptr; Harness::require(candidates->GetString(i, &value), "read candidate");
            const bool matches = std::wstring(value, SysStringLen(value)) == text; SysFreeString(value);
            if (!matches) continue;
            for (UINT step = 0; step < i; ++step) ime.key(VK_DOWN);
            if (!ime.key(VK_SPACE)) throw std::runtime_error("candidate was not selected");
            return;
        }
    }
    throw std::runtime_error("expected candidate not found");
}
pinyin::UserDictionary personal_words(const std::filesystem::path& path) {
    pinyin::UserStore store(path); return store.load();
}
}

int wmain(int argc, wchar_t* argv[]) {
    using namespace pinyin::testing;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int checks = 0;
    auto check = [&](bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); };
    try {
        check(argc == 2 && SUCCEEDED(initialized), "test setup failed");
        TestWorkspace workspace;
        const auto user_path = pinyin::default_user_path();
        // Artificial user word 如荷 stays absent from the base dictionary as it expands.
        check(pinyin::ime_learning_enabled(user_path) && !std::filesystem::exists(user_path.parent_path()),
            "fresh TSF learning settings were not enabled by default");
        {
            Harness ime(argv[1]); ime.type("ruhe"); choose(ime, L"如");
            check(!std::filesystem::exists(user_path), "default learning saved an incomplete phrase");
            choose(ime, L"荷");
            check(ime.store->text == L"如荷" && personal_words(user_path).at({"ru he", "如荷"}) == 1,
                "fresh TSF did not automatically learn without an on command");
            ime.close(); check(ime.unload_result == S_OK, "default learning service leaked DLL references");
        }
        { pinyin::UserStore store(user_path); store.save({}); }
        pinyin::set_ime_learning(user_path, false);
        {
            Harness ime(argv[1]);
            check(!ime.key(VK_SPACE), "idle space was swallowed");
            BYTE actual_keyboard[256]{}, modified_keyboard[256]{};
            check(!!GetKeyboardState(actual_keyboard), "cannot capture test thread keyboard state");
            std::copy(std::begin(actual_keyboard), std::end(actual_keyboard), std::begin(modified_keyboard));
            modified_keyboard[VK_CONTROL] = 0x80; modified_keyboard[VK_CAPITAL] = 1;
            check(!!SetKeyboardState(modified_keyboard), "cannot set test thread modifier fixture");
            ime.type("nihao");
            const bool modifiers_restored = (GetKeyState(VK_CONTROL) & 0x8000) && (GetKeyState(VK_CAPITAL) & 1);
            SetKeyboardState(actual_keyboard);
            check(modifiers_restored, "synthetic keyboard input did not restore the test thread's state");
            check(ime.store->text == L"nihao" && ime.compositions() == 1, "real TSF preedit failed");
            ComPtr<ITfCandidateListUIElement> candidates;
            {
                ComPtr<ITfUIElementMgr> ui; Harness::require(ime.manager.As(&ui), "get UI manager");
                ComPtr<IEnumTfUIElements> enumeration; Harness::require(ui->EnumUIElements(&enumeration), "enumerate UI");
                for (;;) {
                    ComPtr<ITfUIElement> element; ULONG fetched = 0;
                    if (enumeration->Next(1, &element, &fetched) != S_OK || !fetched) break;
                    GUID id{}; element->GetGUID(&id);
                    if (id == candidate_id) { element.As(&candidates); break; }
                }
            }
            check(!!candidates, "TSF candidate UI was not registered");
            UINT count = 0, selected = 99, page = 99;
            candidates->GetCount(&count); candidates->GetSelection(&selected); candidates->GetCurrentPage(&page);
            check(count >= 2 && selected == 0 && page == 0, "candidate UI metadata mismatch");
            BSTR word = nullptr; Harness::require(candidates->GetString(0, &word), "read candidate");
            const std::wstring first(word, SysStringLen(word)); SysFreeString(word);
            check(first == L"你好", "candidate UI text mismatch");
            UINT page_starts[10]{}, pages = 0;
            check(candidates->GetPageIndex(page_starts, 10, &pages) == S_OK &&
                pages == (count + pinyin::InputSession::page_size - 1) / pinyin::InputSession::page_size && page_starts[0] == 0,
                "candidate UI page boundaries mismatch");
            BOOL shown = TRUE;
            Harness::require(candidates->Show(FALSE), "hide candidate UI"); candidates->IsShown(&shown);
            check(!shown && ime.store->text == L"nihao", "hiding candidate UI changed the composition");
            Harness::require(candidates->Show(TRUE), "restore candidate UI"); Harness::pump();
            check(ime.key(VK_SPACE), "space was not handled");
            check(ime.store->text == L"你好" && ime.compositions() == 0, "real TSF candidate commit failed");
            Harness::require(candidates->GetString(0, &word), "read retained candidate snapshot");
            const std::wstring retained(word, SysStringLen(word)); SysFreeString(word);
            check(retained == L"你好", "ended composition invalidated a retained candidate snapshot");
            check(candidates->Show(TRUE) == S_OK, "ended candidate snapshot retained a service callback");
            candidates.Reset();
            ime.type("nihao"); ime.key('2');
            check(ime.store->text == L"你好拟好" && ime.compositions() == 0, "numeric candidate commit failed");
            ime.type("nihao"); ime.key(VK_BACK);
            check(ime.store->text == L"你好拟好niha", "backspace did not update TSF range");
            ime.key(VK_ESCAPE);
            check(ime.store->text == L"你好拟好" && ime.compositions() == 0, "Esc did not remove composition");
            ime.type("nihao"); ime.key(VK_RETURN);
            check(ime.store->text == L"你好拟好nihao" && ime.compositions() == 0, "Enter did not commit raw text");
            ime.type("niho"); ime.key(VK_LEFT); ime.key('A'); ime.key(VK_SPACE);
            check(ime.store->text == L"你好拟好nihao你好", "middle editing did not preserve TSF selection");
            ime.type("n"); ime.key(VK_BACK);
            check(ime.compositions() == 0, "final backspace left a composition");
            ime.type("xian"); ime.key('2');
            check(ime.compositions() == 0, "ambiguous candidate selection did not commit");
            ime.store->readonly = true; check(!ime.key('N'), "read-only document input swallowed"); ime.store->readonly = false;
            ComPtr<ITfCompartmentMgr> compartments; ime.context.As(&compartments);
            ComPtr<ITfCompartment> disabled; compartments->GetCompartment(GUID_COMPARTMENT_KEYBOARD_DISABLED, &disabled);
            VARIANT flag; VariantInit(&flag); flag.vt = VT_I4; flag.lVal = 1; disabled->SetValue(ime.client, &flag);
            check(!ime.key('N'), "disabled text services ignored"); flag.lVal = 0; disabled->SetValue(ime.client, &flag);
            check(ime.key(VK_SHIFT) && ime.key_up(VK_SHIFT), "solo Shift toggle not handled");
            check(!ime.key('N'), "English mode swallowed letter");
            ime.key(VK_SHIFT); ime.key_up(VK_SHIFT); ime.type("nihao");
            ime.keys->OnSetFocus(FALSE); Harness::pump();
            check(ime.compositions() == 0, "focus loss did not end composition");
            check(ime.store->text.size() >= 5 && ime.store->text.substr(ime.store->text.size() - 5) == L"nihao", "focus loss discarded raw text");
            ime.type("nihao");
            check(!ime.key(VK_OEM_COMMA), "punctuation should pass through");
            ime.store->insert_external(L","); Harness::pump();
            check(ime.compositions() == 0, "external edit left a stale composition");
            const auto before = ime.store->text;
            ime.type("nihao"); ime.key(VK_SPACE);
            check(ime.store->text == before + L"你好", "next composition overwrote passed-through punctuation");
            // These words are absent from the original 99-entry fixture: verify
            // that the DLL actually loads and commits the expanded deployed data.
            for (const auto& sample : {std::pair{"shouji", L"手机"},
                                      std::pair{"lvcha", L"绿茶"},
                                      std::pair{"xiexienindebangzhu", L"谢谢您的帮助"}}) {
                const auto previous = ime.store->text;
                ime.type(sample.first); ime.key(VK_SPACE);
                check(ime.store->text == previous + sample.second && ime.compositions() == 0,
                    "expanded deployed dictionary failed to commit");
            }
            const auto before_stagnate = ime.store->text;
            ime.type("tingzhi"); choose(ime, L"停滞");
            check(ime.store->text == before_stagnate + L"停滞" && ime.compositions() == 0,
                "reported missing word 停滞 did not commit as a whole");
            ime.type("tingzhi"); choose(ime, L"停");
            check(ime.store->text == before_stagnate + L"停滞停zhi" && ime.compositions() == 1,
                "selecting 停 lost its unconverted zhi suffix");
            choose(ime, L"滞");
            check(ime.store->text == before_stagnate + L"停滞停滞" && ime.compositions() == 0,
                "reported missing character 滞 was not independently selectable");
            ime.close();
            check(ime.unload_result == S_OK, "activated service leaked DLL references");
        }
        check(personal_words(user_path).empty(), "explicitly disabled TSF mode saved personal words");
        pinyin::set_ime_learning(user_path, true);
        {
            Harness ime(argv[1]);
            ime.type("ruhe"); choose(ime, L"如");
            check(ime.store->text == L"如he" && ime.compositions() == 1, "partial selection did not retain the second syllable");
            check(personal_words(user_path).empty(), "partial TSF selection was learned before completion");
            choose(ime, L"荷");
            check(ime.store->text == L"如荷" && ime.compositions() == 0, "independent second-character commit failed");
            check(personal_words(user_path).at({"ru he", "如荷"}) == 1, "confirmed TSF phrase was not automatically learned");
            ime.close(); check(ime.unload_result == S_OK, "learning service leaked DLL references");
        }
        {
            // A learned custom word remains selectable alongside the newly
            // curated common homophone 如何, whose base priority stays higher.
            Harness ime(argv[1]); ime.type("ruhe"); choose(ime, L"如荷");
            check(ime.store->text == L"如荷" && ime.compositions() == 0, "restarted TSF did not offer the learned whole word");
            check(personal_words(user_path).at({"ru he", "如荷"}) == 2, "restarted TSF learning lost the selection count");
            const auto previous = personal_words(user_path);
            ime.type("ru'he"); choose(ime, L"如"); ime.key(VK_ESCAPE);
            check(ime.store->text == L"如荷" && personal_words(user_path) == previous, "cancelled segmented TSF input was learned");
            ime.type("ru'he"); choose(ime, L"如"); ime.key(VK_RETURN);
            check(ime.store->text == L"如荷如he" && personal_words(user_path) == previous, "raw Enter submission was learned");
            ime.type("ru'he"); choose(ime, L"如"); ime.keys->OnSetFocus(FALSE); Harness::pump();
            check(ime.compositions() == 0 && personal_words(user_path) == previous, "focus loss learned unfinished input");
            pinyin::set_ime_learning(user_path, false);
            ime.type("ruhe"); choose(ime, L"如");
            check(ime.store->text.size() >= 4 && ime.store->text.substr(ime.store->text.size() - 3) == L"如he",
                "turning learning off retained the whole personal word");
            choose(ime, L"荷"); check(personal_words(user_path) == previous, "disabled TSF learning still saved words");
            ime.close(); check(ime.unload_result == S_OK, "restarted learning service leaked DLL references");
        }
        pinyin::set_ime_learning(user_path, true);
        const auto before_private = personal_words(user_path);
        for (const auto scope : {IS_PRIVATE, IS_PASSWORD, IS_NUMERIC_PASSWORD, IS_NUMERIC_PIN,
                                 IS_ALPHANUMERIC_PIN, IS_ALPHANUMERIC_PIN_SET}) {
            Harness ime(argv[1], scope);
            check(!ime.key('N'), "sensitive InputScope swallowed a letter");
            check(ime.store->text.empty() && ime.compositions() == 0, "sensitive InputScope started a composition");
            check(personal_words(user_path) == before_private, "sensitive InputScope changed personal words");
            ime.close();
            check(ime.unload_result == S_OK, "sensitive context leaked service references");
        }
        CoUninitialize(); std::cout << "PASS: " << checks << " real TSF integration checks\n"; return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        if (SUCCEEDED(initialized)) CoUninitialize(); return 1;
    }
}
