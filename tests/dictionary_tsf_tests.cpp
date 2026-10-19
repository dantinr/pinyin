#include "tsf_text_store.hpp"
#include "test_workspace.hpp"
#include "pinyin/dictionary_manager.hpp"
#include "pinyin/learning_dictionary.hpp"
#include "pinyin/user_store.hpp"
#include <fstream>
#include <iostream>

namespace {
int checks = 0;
void check(bool value, const char* message) { ++checks; if (!value) throw std::runtime_error(message); }
bool candidate(pinyin::testing::Harness& ime, const wchar_t* text, bool choose = false) {
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
            const bool match = std::wstring(value, SysStringLen(value)) == text; SysFreeString(value);
            if (match) {
                if (choose) {
                    for (UINT step = 0; step < index; ++step) Harness::require(ime.key(VK_DOWN) ? S_OK : E_FAIL, "select candidate");
                    Harness::require(ime.key(VK_SPACE) ? S_OK : E_FAIL, "confirm candidate");
                }
                return true;
            }
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
        const auto imported = workspace.root / L"personal.tsv";
        { std::ofstream file(imported, std::ios::binary); file << "个人导入词ABC\the bing ce shi ci\t4\n"; }
        merge_user_dictionary(user, imported);
        ime.type("hebingceshici");
        check(!candidate(ime, L"个人导入词ABC"), "merge turned learning on against the user's setting"); ime.key(VK_ESCAPE);
        set_ime_learning(user, true); ime.type("hebingceshici");
        check(candidate(ime, L"个人导入词ABC"), "running TSF did not load a merged personal word in the next composition");
        check(ime.key(VK_SPACE) && ime.store->text == L"辅库测试词个人导入词ABC" && ime.compositions() == 0,
            "merged personal candidate did not commit");
        check(read_user_dictionary(user).at({"he bing ce shi ci", "个人导入词ABC"}) == 5,
            "merged personal word did not continue learning from the imported count");
        const auto main_result = merge_user_into_main(
            std::filesystem::path(argv[1]).parent_path() / L"data" / L"base.tsv", user);
        check(main_result.added == 1, "personal word was not promoted into the main dictionary");
        const auto before_main = read_user_dictionary(user);
        set_ime_learning(user, false); ime.type("hebingceshici");
        check(candidate(ime, L"个人导入词ABC"), "running TSF did not load a merged main word with learning disabled");
        check(ime.key(VK_SPACE) && ime.store->text == L"辅库测试词个人导入词ABC个人导入词ABC" &&
            read_user_dictionary(user) == before_main,
            "main word did not commit independently of personal learning");
        ime.close(); check(ime.unload_result == S_OK, "dictionary reload leaked DLL references");
        set_ime_learning(user, true);
        {
            UserStore store(user);
            auto users = store.load(); users[{"tong yi hou tai", "统一后台"}] = 1; store.save(users);
        }
        {
            Harness phrase_ime(argv[1]);
            phrase_ime.type("tongyi");
            check(candidate(phrase_ime, L"统一", true) && phrase_ime.store->text == L"统一" &&
                read_user_dictionary(user).at({"tong yi", "统一"}) == 1,
                "real TSF could not recover and persist a short word from a previously learned phrase");
            phrase_ime.type("tongyihoutai");
            check(candidate(phrase_ime, L"统一", true), "real TSF lost a confirmed phrase prefix");
            check(candidate(phrase_ime, L"后台", true) && phrase_ime.store->text == L"统一统一后台" &&
                read_user_dictionary(user).at({"tong yi", "统一"}) == 2,
                "real TSF lost a selected word when the full phrase committed");
            phrase_ime.close(); check(phrase_ime.unload_result == S_OK, "phrase learning leaked DLL references");
        }
        {
            Harness restarted(argv[1]); restarted.type("tongyi");
            check(restarted.key(VK_SPACE) && restarted.store->text == L"统一",
                "real TSF did not retain the learned short word in a new application");
            restarted.close(); check(restarted.unload_result == S_OK, "restarted phrase learning leaked DLL references");
        }
        CoUninitialize(); std::cout << "PASS: " << checks << " real TSF supplementary dictionary checks\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
        if (SUCCEEDED(initialized)) CoUninitialize(); return 1;
    }
}
