#include "tsf_text_store.hpp"
#include "test_workspace.hpp"
#include "pinyin/dictionary_manager.hpp"
#include "pinyin/learning_dictionary.hpp"
#include <iostream>

namespace {
int checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
bool candidate(pinyin::testing::Harness& ime, const wchar_t* text) {
    using namespace pinyin::testing;
    ComPtr<ITfUIElementMgr> ui; Harness::require(ime.manager.As(&ui), "get UI manager");
    ComPtr<IEnumTfUIElements> enumeration; Harness::require(ui->EnumUIElements(&enumeration), "enumerate UI");
    for (;;) {
        ComPtr<ITfUIElement> element; ULONG fetched = 0;
        if (enumeration->Next(1, &element, &fetched) != S_OK || !fetched) break;
        GUID id{}; element->GetGUID(&id); if (id != candidate_id) continue;
        ComPtr<ITfCandidateListUIElement> list; Harness::require(element.As(&list), "get candidates");
        UINT count = 0; list->GetCount(&count);
        for (UINT index = 0; index < count; ++index) {
            BSTR value = nullptr; Harness::require(list->GetString(index, &value), "read candidate");
            const bool match = std::wstring(value, SysStringLen(value)) == text; SysFreeString(value); if (match) return true;
        }
    }
    return false;
}
}
int wmain(int argc, wchar_t* argv[]) {
    using namespace pinyin; using namespace pinyin::testing;
    const auto initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    try {
        check(argc == 2 && SUCCEEDED(initialized), "test setup failed");
        TestWorkspace workspace; const auto user = default_user_path();
        set_ime_learning(user, false); DictionaryManager manager(default_dictionary_directory());
        Harness ime(argv[1]);
        manager.upsert("agent", {"辅库测试词", "fu ku ce shi ci", 300});
        ime.type("fukuceshici");
        check(candidate(ime, L"辅库测试词"), "running TSF did not load an Agent-created dictionary");
        check(ime.key(VK_SPACE) && ime.store->text == L"辅库测试词" && ime.compositions() == 0 && !std::filesystem::exists(user),
            "auxiliary candidate could not commit with learning disabled");
        manager.set_enabled("agent", false); ime.type("fukuceshici");
        check(!candidate(ime, L"辅库测试词"), "disabled dictionary remained in the next composition"); ime.key(VK_ESCAPE);
        manager.set_enabled("agent", true); ime.type("fukuceshici");
        check(candidate(ime, L"辅库测试词"), "re-enabled dictionary did not return"); ime.key(VK_ESCAPE);
        manager.upsert("agent", {"辅库更新词", "fu ku geng xin ci", 300}); ime.type("fukugengxinci");
        check(candidate(ime, L"辅库更新词"), "Agent additions required restarting the application"); ime.key(VK_ESCAPE);
        manager.remove("agent", "辅库测试词", "fu ku ce shi ci"); ime.type("fukuceshici");
        check(!candidate(ime, L"辅库测试词"), "Agent-removed word survived the next composition"); ime.key(VK_ESCAPE);
        ime.close(); check(ime.unload_result == S_OK, "dictionary reload leaked DLL references");
        CoUninitialize(); std::cout << "PASS: " << checks << " real TSF supplementary dictionary checks\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        if (SUCCEEDED(initialized)) CoUninitialize(); return 1;
    }
}
