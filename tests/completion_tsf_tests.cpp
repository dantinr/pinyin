#include "tsf_text_store.hpp"
#include "test_workspace.hpp"
#include "pinyin/ime_settings.hpp"
#include <iostream>

namespace {
int checks = 0;
void check(bool condition, const char* message) { ++checks; if (!condition) throw std::runtime_error(message); }
pinyin::UserDictionary words(const std::filesystem::path& path) { pinyin::UserStore store(path); return store.load(); }
pinyin::ime::ComPtr<ITfCandidateListUIElement> candidates(pinyin::testing::Harness& ime) {
    using namespace pinyin::ime;
    ComPtr<ITfUIElementMgr> manager; pinyin::testing::Harness::require(ime.manager.As(&manager), "get candidate manager");
    ComPtr<IEnumTfUIElements> enumeration; manager->EnumUIElements(&enumeration);
    for (;;) {
        ComPtr<ITfUIElement> element; ULONG count = 0;
        if (enumeration->Next(1, &element, &count) != S_OK || !count) break;
        GUID id{}; element->GetGUID(&id);
        if (id == candidate_id) { ComPtr<ITfCandidateListUIElement> result; element.As(&result); return result; }
    }
    throw std::runtime_error("no active candidate UI");
}
std::wstring item(ITfCandidateListUIElement* ui, UINT index) {
    BSTR value = nullptr; pinyin::testing::Harness::require(ui->GetString(index, &value), "read candidate text");
    std::wstring text(value, SysStringLen(value)); SysFreeString(value); return text;
}
void choose(pinyin::testing::Harness& ime, const std::wstring& text) {
    auto ui = candidates(ime); UINT count = 0, page = 0; ui->GetCount(&count); ui->GetCurrentPage(&page);
    for (UINT i = 0; i < count; ++i) {
        if (item(ui.Get(), i) != text) continue;
        while (page < i / pinyin::InputSession::page_size) { ime.key(VK_NEXT); ++page; }
        while (page > i / pinyin::InputSession::page_size) { ime.key(VK_PRIOR); --page; }
        check(ime.key('1' + i % pinyin::InputSession::page_size), "completed candidate digit was not handled"); return;
    }
    throw std::runtime_error("requested candidate not found");
}
}
int wmain(int argc, wchar_t* argv[]) {
    using namespace pinyin; using namespace pinyin::testing;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(argc == 2 && SUCCEEDED(initialized), "test setup failed");
        TestWorkspace workspace; const auto path = default_user_path();
        {
            Harness ime(argv[1]); ime.type("niha");
            check(ime.store->text == L"niha" && ime.compositions() == 1 && !std::filesystem::exists(path),
                "unconfirmed completion changed text or saved personal data");
            { auto ui = candidates(ime); check(item(ui.Get(), 0) == L"你好", "completed word missing from real TSF candidate UI"); }
            ime.key(VK_SPACE);
            check(ime.store->text == L"你好" && ime.compositions() == 0 && words(path).at({"ni hao", "你好"}) == 1,
                "space did not confirm completion with its full reading");
            ime.type("beijin"); ime.key(VK_OEM_COMMA);
            check(ime.store->text == L"你好北京，" && words(path).at({"bei jing", "北京"}) == 1,
                "completion punctuation lost canonical reading or candidate confirmation");
            ime.type("ni'ha"); ime.key('2');
            check(ime.store->text == L"你好北京，拟好" && words(path).at({"ni hao", "拟好"}) == 1,
                "numeric selection of a completed candidate failed");
            ime.type("bjin"); choose(ime, L"北京");
            check(ime.store->text == L"你好北京，拟好北京" && words(path).at({"bei jing", "北京"}) == 2,
                "mixed completion created a separate learning record or consumed wrong offsets");
            ime.close(); check(ime.unload_result == S_OK, "completion candidate UI leaked DLL references");
        }
        {
            Harness ime(argv[1]); const auto previous = words(path);
            ime.type("niha"); choose(ime, L"你");
            check(ime.store->text == L"你ha" && ime.compositions() == 1 && words(path) == previous,
                "prefix confirmation lost incomplete remainder or learned too soon");
            ime.key(VK_HOME);
            check(ime.store->text == L"niha", "Home restored untyped completion letters");
            ime.key(VK_END); choose(ime, L"你"); ime.key(VK_LEFT); ime.key(VK_LEFT); ime.key(VK_BACK);
            check(ime.store->text == L"niha", "segment undo did not restore actual typed spelling");
            ime.key(VK_ESCAPE);
            check(ime.store->text.empty() && words(path) == previous, "cancelled completion was learned");
            ime.type("niha"); choose(ime, L"你"); choose(ime, L"好");
            check(ime.store->text == L"你好" && words(path).at({"ni hao", "你好"}) == previous.at({"ni hao", "你好"}) + 1,
                "independent final-character completion did not share the full word's record");
            ime.close(); check(ime.unload_result == S_OK, "segmented completion leaked DLL references");
        }
        {
            Harness ime(argv[1]); const auto previous = words(path);
            ime.type("beijin"); ime.store->reject_writes = true;
            check(!ime.key(VK_SPACE) && ime.store->text == L"beijin" && words(path) == previous,
                "rejected completion write consumed input or saved a word");
            ime.store->reject_writes = false; ime.key(VK_SPACE);
            check(ime.store->text == L"北京" && ime.compositions() == 0, "rejected completion could not recover");
            const auto confirmed = words(path);
            ime.type("niha"); ime.key(VK_RETURN); ime.type("beijin"); ime.key(VK_ESCAPE);
            check(ime.store->text == L"北京niha" && words(path) == confirmed, "raw Enter or cancel learned a completion");
            ime.type("beijin"); ime.keys->OnSetFocus(FALSE); Harness::pump();
            check(ime.store->text == L"北京nihabeijin" && ime.compositions() == 0 && words(path) == confirmed,
                "focus loss committed or learned an unselected completion");
            ime.close(); check(ime.unload_result == S_OK, "rejected completion leaked DLL references");
        }
        {
            Harness ime(argv[1]); ime.type("wozaibeijin"); ime.key(VK_SPACE);
            check(ime.store->text == L"我在北京" && words(path).count({"wo zai bei jing", "我在北京"}) == 1,
                "sentence tail completion did not save full syllables");
            ime.close(); check(ime.unload_result == S_OK, "sentence completion leaked DLL references");
        }
        {
            set_ime_learning(path, false); set_chinese_punctuation(path, false);
            const auto previous = words(path); Harness ime(argv[1]); ime.type("niha"); ime.key(VK_OEM_COMMA);
            check(ime.store->text == L"你好," && words(path) == previous, "completion bypassed punctuation or learning setting");
            ime.key(VK_SHIFT); ime.key_up(VK_SHIFT);
            check(!ime.key('N') && !ime.key('H'), "English mode intercepted incomplete pinyin");
            ime.close(); check(ime.unload_result == S_OK, "settings completion leaked DLL references");
        }
        for (const auto scope : {IS_PASSWORD, IS_NUMERIC_PIN}) {
            Harness ime(argv[1], scope); const auto previous = words(path);
            check(!ime.key('N') && !ime.key('H') && ime.store->text.empty() && words(path) == previous,
                "completion intercepted password/PIN input");
            ime.close(); check(ime.unload_result == S_OK, "sensitive completion leaked DLL references");
        }
        CoUninitialize(); std::cout << "PASS: " << checks << " real TSF completion checks\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        if (SUCCEEDED(initialized)) CoUninitialize(); return 1;
    }
}
