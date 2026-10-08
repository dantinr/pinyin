#include "tsf_text_store.hpp"
#include <iostream>

int wmain(int argc, wchar_t* argv[]) {
    using namespace pinyin::testing;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    int checks = 0;
    auto check = [&](bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); };
    try {
        check(argc == 2 && SUCCEEDED(initialized), "test setup failed");
        {
            Harness ime(argv[1]);
            check(!ime.key(VK_SPACE), "idle space was swallowed");
            ime.type("nihao");
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
            check(count == 2 && selected == 0 && page == 0, "candidate UI metadata mismatch");
            BSTR word = nullptr; Harness::require(candidates->GetString(0, &word), "read candidate");
            const std::wstring first(word, SysStringLen(word)); SysFreeString(word);
            check(first == L"你好", "candidate UI text mismatch");
            UINT page_start = 99, pages = 0;
            check(candidates->GetPageIndex(&page_start, 1, &pages) == S_OK && pages == 1 && page_start == 0,
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
            ime.close();
            check(ime.unload_result == S_OK, "activated service leaked DLL references");
        }
        for (const auto scope : {IS_PRIVATE, IS_PASSWORD, IS_NUMERIC_PASSWORD, IS_NUMERIC_PIN,
                                 IS_ALPHANUMERIC_PIN, IS_ALPHANUMERIC_PIN_SET}) {
            Harness ime(argv[1], scope);
            check(!ime.key('N'), "sensitive InputScope swallowed a letter");
            check(ime.store->text.empty() && ime.compositions() == 0, "sensitive InputScope started a composition");
            ime.close();
            check(ime.unload_result == S_OK, "sensitive context leaked service references");
        }
        CoUninitialize(); std::cout << "PASS: " << checks << " real TSF integration checks\n"; return 0;
    } catch (const std::exception& e) {
        std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n';
        if (SUCCEEDED(initialized)) CoUninitialize(); return 1;
    }
}
