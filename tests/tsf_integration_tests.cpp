#include "tsf_text_store.hpp"
#include "test_workspace.hpp"
#include "pinyin/learning_dictionary.hpp"
#include <iostream>

namespace {
struct TestWindow {
    HWND window = CreateWindowExW(WS_EX_NOACTIVATE | WS_EX_TOOLWINDOW, L"STATIC", L"Private Pinyin focus test",
        WS_POPUP | WS_VISIBLE, 24, 24, 640, 180, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    TestWindow() { if (!window) throw std::runtime_error("cannot create test document window"); }
    ~TestWindow() { if (window) DestroyWindow(window); }
};
struct CandidateWindows { unsigned count = 0; HWND first = nullptr; };
BOOL CALLBACK collect_candidate(HWND window, LPARAM parameter) {
    wchar_t name[80]{}; GetClassNameW(window, name, 80);
    if (std::wstring(name) == L"PrivatePinyin.Candidate.4ea569f1" && IsWindowVisible(window)) {
        auto& result = *reinterpret_cast<CandidateWindows*>(parameter);
        ++result.count; result.first = window;
    }
    return TRUE;
}
CandidateWindows visible_candidates() {
    CandidateWindows result;
    EnumThreadWindows(GetCurrentThreadId(), collect_candidate, reinterpret_cast<LPARAM>(&result));
    return result;
}
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
            TestWindow owner; Harness ime(argv[1]); ime.store->window = owner.window;
            ComPtr<ITfTextLayoutSink> layout; Harness::require(ime.service.As(&layout), "get layout sink");
            ComPtr<ITfThreadFocusSink> focus; Harness::require(ime.service.As(&focus), "get thread focus sink");
            ime.type("nihao");
            const auto popup = visible_candidates();
            check(popup.count == 1, "real TSF did not display a native candidate popup");
            ime.store->defer_locks = true;
            Harness::require(layout->OnLayoutChange(ime.context.Get(), TF_LC_CHANGE, nullptr), "queue layout refresh");
            Harness::pump();
            check(ime.store->pending_locks() > 0, "layout refresh did not wait for the application read lock");
            Harness::require(focus->OnKillThreadFocus(), "lose UI thread focus");
            check(!IsWindow(popup.first) && visible_candidates().count == 0,
                "thread focus loss retained the native popup while waiting for a write lock");
            Harness::pump();
            check(ime.compositions() == 1, "delayed write fixture ended the composition before granting its lock");
            bool reopened = false;
            ime.store->resume_locks([&] { reopened = reopened || visible_candidates().count != 0; });
            Harness::pump();
            check(!reopened && visible_candidates().count == 0 && ime.compositions() == 0 && ime.store->text == L"nihao",
                "stale layout callback reopened the candidate popup after focus loss");
            Harness::require(focus->OnSetThreadFocus(), "regain UI thread focus");
            ime.type("nihao"); check(visible_candidates().count == 1, "new composition did not recreate its popup after focus return");
            ime.key(VK_SPACE);
            check(ime.store->text == L"nihao你好" && visible_candidates().count == 0, "commit left a visible candidate popup");
            ime.type("nihao"); ime.key(VK_ESCAPE);
            check(visible_candidates().count == 0 && ime.store->text == L"nihao你好", "cancel left a candidate popup");
            ime.type("nihao"); ime.store->defer_locks = true;
            Harness::require(layout->OnLayoutChange(ime.context.Get(), TF_LC_CHANGE, nullptr), "queue layout before deactivation");
            Harness::pump();
            Harness::require(ime.service->Deactivate(), "deactivate with pending layout");
            reopened = false;
            ime.store->resume_locks([&] { reopened = reopened || visible_candidates().count != 0; });
            Harness::pump();
            check(!reopened && visible_candidates().count == 0 && ime.compositions() == 0,
                "pending layout callback recreated a popup after service deactivation");
            layout.Reset(); focus.Reset(); ime.store->window = nullptr;
            DestroyWindow(owner.window); owner.window = nullptr;
            ime.close(); check(ime.unload_result == S_OK, "thread focus/layout subscriptions leaked DLL references");
        }
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
            check(ime.key(VK_OEM_COMMA), "Chinese punctuation was not handled");
            check(ime.compositions() == 0, "punctuation left an active composition");
            ime.type("nihao"); ime.store->insert_external(L"@"); Harness::pump();
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
            const auto before_sentence = ime.store->text;
            ime.type("wozaibeijingshangban");
            check(ime.store->text == before_sentence + L"wozaibeijingshangban" && ime.compositions() == 1,
                "whole-sentence preedit failed");
            ime.key(VK_SPACE);
            check(ime.store->text == before_sentence + L"我在北京上班" && ime.compositions() == 0,
                "whole-sentence first candidate did not commit through real TSF");
            ime.type("wozaibeijingshangban"); choose(ime, L"我");
            check(ime.store->text == before_sentence + L"我在北京上班我zaibeijingshangban" && ime.compositions() == 1,
                "sentence prefix correction lost unconverted text");
            choose(ime, L"在"); choose(ime, L"背景"); ime.key(VK_SPACE);
            check(ime.store->text == before_sentence + L"我在北京上班我在背景上班" && ime.compositions() == 0,
                "manual sentence correction did not commit through real TSF");
            for (const auto& sample : {std::pair{"nh", L"你好"}, std::pair{"nhao", L"你好"},
                                      std::pair{"bj", L"北京"}, std::pair{"zhg", L"中国"},
                                      std::pair{"chq", L"重庆"}, std::pair{"shh", L"上海"},
                                      std::pair{"wozaibjshangb", L"我在北京上班"}}) {
                const auto previous = ime.store->text;
                ime.type(sample.first); choose(ime, sample.second);
                check(ime.store->text == previous + sample.second && ime.compositions() == 0,
                    "short/mixed candidate did not commit through real TSF");
            }
            ime.close();
            check(ime.unload_result == S_OK, "activated service leaked DLL references");
        }
        check(personal_words(user_path).empty(), "explicitly disabled TSF mode saved personal words");
        pinyin::set_ime_learning(user_path, true);
        {
            Harness ime(argv[1]);
            const auto before_sentence = personal_words(user_path);
            ime.type("wozaibeijingshangban"); ime.key(VK_ESCAPE);
            check(ime.store->text.empty() && personal_words(user_path) == before_sentence,
                "cancelled automatic sentence was learned");
            ime.type("wozaibeijingshangban"); ime.key(VK_RETURN);
            check(ime.store->text == L"wozaibeijingshangban" && personal_words(user_path) == before_sentence,
                "raw sentence Enter submission was learned");
            ime.type("wozaibeijingshangban"); ime.key(VK_SPACE);
            check(ime.store->text == L"wozaibeijingshangban我在北京上班" && ime.compositions() == 0 &&
                personal_words(user_path).at({"wo zai bei jing shang ban", "我在北京上班"}) == 1,
                "selected TSF sentence was not learned");
            ime.close(); check(ime.unload_result == S_OK, "sentence service leaked DLL references");
        }
        {
            Harness ime(argv[1]); ime.type("wozaibeijingshangban"); ime.key(VK_SPACE);
            check(ime.store->text == L"我在北京上班" &&
                personal_words(user_path).at({"wo zai bei jing shang ban", "我在北京上班"}) == 2,
                "restarted TSF did not reuse a selected sentence");
            ime.type("wzbjsb"); ime.key(VK_SPACE);
            check(ime.store->text == L"我在北京上班我在北京上班" &&
                personal_words(user_path).at({"wo zai bei jing shang ban", "我在北京上班"}) == 3,
                "restarted TSF did not reuse a full sentence by its initials");
            ime.close(); check(ime.unload_result == S_OK, "restarted sentence service leaked DLL references");
        }
        std::string long_spelling, long_reading, long_utf8;
        std::wstring long_text;
        for (int i = 0; i < 80; ++i) {
            long_spelling += "wo"; long_utf8 += "我"; long_text += L"我";
            if (i) long_reading += ' ';
            long_reading += "wo";
        }
        {
            Harness ime(argv[1]); ime.type(long_spelling.c_str());
            check(ime.store->text == wide(long_spelling) && ime.compositions() == 1,
                "TSF stopped accepting a preedit beyond 128 characters");
            ime.key(VK_SPACE);
            check(ime.store->text == long_text && ime.compositions() == 0 &&
                personal_words(user_path).at({long_reading, long_utf8}) == 1,
                "long TSF sentence failed to commit and learn");
            ime.close(); check(ime.unload_result == S_OK, "long sentence service leaked DLL references");
        }
        {
            Harness ime(argv[1]); ime.type(long_spelling.c_str()); ime.key(VK_SPACE);
            check(ime.store->text == long_text && personal_words(user_path).at({long_reading, long_utf8}) == 2,
                "restarted TSF did not reuse a long learned sentence");
            ime.close(); check(ime.unload_result == S_OK, "restarted long sentence service leaked DLL references");
        }
        { pinyin::UserStore store(user_path); store.save({}); }
        {
            Harness ime(argv[1]); ime.type("rh"); choose(ime, L"如");
            check(ime.store->text == L"如h" && ime.compositions() == 1 && personal_words(user_path).empty(),
                "short TSF prefix lost its remainder or learned too early");
            choose(ime, L"荷");
            check(ime.store->text == L"如荷" && ime.compositions() == 0 &&
                personal_words(user_path).size() == 1 && personal_words(user_path).at({"ru he", "如荷"}) == 1,
                "short TSF phrase did not save its full pronunciation");
            ime.close(); check(ime.unload_result == S_OK, "short learning service leaked DLL references");
        }
        {
            Harness ime(argv[1]); ime.type("rh"); ime.key(VK_SPACE);
            check(ime.store->text == L"如荷" && personal_words(user_path).at({"ru he", "如荷"}) == 2,
                "restarted TSF did not reuse abbreviated personal word");
            ime.type("ruhe"); ime.key(VK_SPACE);
            check(ime.store->text == L"如荷如荷" && personal_words(user_path).size() == 1 &&
                personal_words(user_path).at({"ru he", "如荷"}) == 3, "short/full TSF input created separate personal words");
            const auto previous = personal_words(user_path);
            ime.type("nh"); ime.key(VK_ESCAPE);
            ime.type("nh"); ime.key(VK_RETURN);
            check(ime.store->text == L"如荷如荷nh" && personal_words(user_path) == previous,
                "cancelled or raw abbreviated TSF input was learned");
            ime.close(); check(ime.unload_result == S_OK, "restarted short service leaked DLL references");
        }
        { pinyin::UserStore store(user_path); store.save({}); }
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
            // A new personal word becomes the first candidate immediately.
            Harness ime(argv[1]); ime.type("ruhe"); ime.key(VK_SPACE);
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
        {
            Harness ime(argv[1], IS_PRIVATE); ime.type("nihao");
            check(ime.store->text == L"nihao" && ime.compositions() == 1,
                "private text field blocked Chinese composition");
            ime.key(VK_SPACE);
            check(ime.store->text == L"你好" && personal_words(user_path).at({"ni hao", "你好"}) == 1,
                "private text field did not commit and learn the selected word");
            ime.close(); check(ime.unload_result == S_OK, "private text context leaked service references");
        }
        {
            Harness ime(argv[1]); const auto previous = personal_words(user_path);
            // Let the application receive shifted letters with its keyboard
            // layout's casing; holding Shift is not a solo mode-switch gesture.
            check(ime.key(VK_SHIFT), "Shift press was not handled");
            std::wstring capitals;
            for (WPARAM letter = 'A'; letter <= 'Z'; ++letter) {
                check(!ime.modified_key(letter, {VK_SHIFT}), "Shift+letter was consumed as lowercase pinyin");
                const std::wstring text(1, static_cast<wchar_t>(letter));
                ime.store->insert_external(text); Harness::pump(); capitals += text;
            }
            check(!ime.key_up(VK_SHIFT) && ime.store->text == capitals && ime.compositions() == 0,
                "shifted alphabet changed mode or started a composition");
            check(ime.key('N'), "Shift+letters switched away from Chinese mode"); ime.key(VK_ESCAPE);

            ime.type("niha"); ime.key(VK_SHIFT);
            check(!ime.modified_key('B', {VK_SHIFT}), "shifted letter inside preedit was swallowed");
            ime.store->insert_external(L"B"); Harness::pump();
            check(!ime.key_up(VK_SHIFT) && ime.store->text == capitals + L"nihaB" && ime.compositions() == 0,
                "application uppercase edit lost raw pinyin or left a stale composition");

            ime.type("nihao"); ime.key(VK_LEFT); ime.key(VK_LEFT); ime.key(VK_SHIFT);
            check(!ime.modified_key('C', {VK_SHIFT}), "shifted letter at the preedit caret was swallowed");
            ime.store->insert_external(L"C"); Harness::pump(); ime.key_up(VK_SHIFT);
            check(ime.store->text == capitals + L"nihaBnihCao" && ime.compositions() == 0,
                "uppercase edit at the caret lost remaining pinyin");

            ime.type("niha"); choose(ime, L"你"); ime.key(VK_SHIFT);
            check(!ime.modified_key('D', {VK_SHIFT}), "shifted letter after a selected segment was swallowed");
            ime.store->insert_external(L"D"); Harness::pump(); ime.key_up(VK_SHIFT);
            // The caret remains after C, before the preserved "ao" suffix.
            check(ime.store->text == capitals + L"nihaBnihC你haDao" && ime.compositions() == 0 &&
                personal_words(user_path) == previous, "uppercase edit lost a segment or learned unconfirmed text");

            check(!ime.modified_key('E', {VK_SHIFT, VK_CAPITAL}) &&
                !ime.modified_key('F', {VK_SHIFT, VK_CONTROL}) && !ime.modified_key('G', {VK_SHIFT, VK_MENU}),
                "CapsLock or modifier shortcuts were intercepted");
            check(ime.key(VK_SHIFT) && ime.key_up(VK_SHIFT) && !ime.key('N'),
                "solo Shift no longer switched to English mode");
            ime.key(VK_SHIFT);
            check(!ime.modified_key('H', {VK_SHIFT}) && !ime.key_up(VK_SHIFT) && !ime.key('N'),
                "Shift+letter changed English mode");
            check(ime.key(VK_SHIFT) && ime.key_up(VK_SHIFT) && ime.key('N'),
                "solo Shift no longer switched back to Chinese mode");
            ime.key(VK_ESCAPE);
            ime.close(); check(ime.unload_result == S_OK, "shifted-letter service leaked DLL references");
        }
        const auto before_private = personal_words(user_path);
        for (const auto scope : {IS_PASSWORD, IS_NUMERIC_PASSWORD, IS_NUMERIC_PIN,
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
